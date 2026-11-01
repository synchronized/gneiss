// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
// 用法：node run_browser_files.cjs <生成的测试.js> <Chromium/Edge路径>
// 需要支持 fetch/WebSocket 的 Node；只运行小型浏览器后端契约，不加载引擎场景。
const {spawn} = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const [artifact, browserPath] = process.argv.slice(2);
if (!artifact || !browserPath) throw Error('缺少测试 JS 或浏览器路径');
const root = path.dirname(path.resolve(artifact));
const name = path.basename(artifact);
const server = http.createServer((request, response) => {
  const route = request.url.split('?')[0];
  if (route === '/empty') {
    response.writeHead(200, {'Content-Length':0}); response.end(); return;
  }
  if (route === '/stream') {
    const bytes = Buffer.from(Array.from({length:257}, (_, i) => i % 251));
    response.writeHead(200, {'Content-Type':'application/octet-stream'});
    response.write(bytes.subarray(0, 91));
    response.write(bytes.subarray(91, 173));
    response.end(bytes.subarray(173));
    return;
  }
  if (route === '/stall') {
    response.writeHead(200, {'Content-Type':'application/octet-stream'});
    response.flushHeaders();
    const timer = setTimeout(() => response.end(Buffer.from([1])), 5000);
    response.on('close', () => clearTimeout(timer));
    return;
  }
  if (['/bytes','/ignore','/bad','/missing'].includes(route)) {
    let data = Buffer.from([1,2,3,4,5]);
    let status = 200;
    if (route === '/missing') { status = 404; data = Buffer.alloc(0); }
    else if (request.headers.range && route !== '/ignore') {
      status = 206; data = data.subarray(1,4);
      response.setHeader('Content-Range', route === '/bad' ? 'bytes 0-2/5' : 'bytes 1-3/5');
    }
    response.writeHead(status, {'Content-Length':data.length});
    response.end(request.method === 'HEAD' ? undefined : data);
    return;
  }
  if (route === '/') {
    response.writeHead(200, {'Content-Type':'text/html'});
    response.end(`<html><body>RUNNING<script>window.onerror=m=>document.body.textContent=m;window.onunhandledrejection=e=>document.body.textContent=String(e.reason);</script><script src="/${name}"></script></body></html>`);
    return;
  }
  const file = path.resolve(root, '.' + route);
  if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    response.writeHead(404); response.end(); return;
  }
  response.writeHead(200, {'Content-Type':file.endsWith('.wasm') ? 'application/wasm' : 'text/javascript'});
  fs.createReadStream(file).pipe(response);
});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
(async () => {
  let browser, socket;
  try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/`;
    // 测试产物目录中的独立配置，不访问用户浏览器配置；保留供失败排查。
    const profile = fs.mkdtempSync(path.join(root, 'browser-files-profile-'));
    browser = spawn(browserPath, ['--headless','--disable-gpu','--no-first-run',
      '--remote-debugging-port=0','--user-data-dir='+profile,url], {windowsHide:true,stdio:'ignore'});
    let target;
    for (let i=0; i<150; ++i) {
      try {
        const port = fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];
        target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(item => item.url === url);
      } catch {}
      if (target) break;
      await delay(100);
    }
    if (!target) throw Error('浏览器连接超时');
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve,reject) => { socket.onopen=resolve; socket.onerror=reject; });
    let id=0;
    const pending = new Map();
    socket.onmessage = event => {
      const message = JSON.parse(event.data);
      if (pending.has(message.id)) { pending.get(message.id)(message); pending.delete(message.id); }
      else if (message.method === 'Runtime.exceptionThrown') console.error(JSON.stringify(message));
    };
    const call = (method,params={}) => new Promise(resolve => {
      const next=++id; pending.set(next,resolve); socket.send(JSON.stringify({id:next,method,params}));
    });
    await call('Runtime.enable');
    for (let i=0; i<150; ++i) {
      const result = await call('Runtime.evaluate',{expression:'document.body.textContent',returnByValue:true});
      const text=result.result?.result?.value;
      if (text === 'PASS') { console.log('PASS'); return; }
      if (text === 'FAIL' || text?.startsWith('Uncaught')) throw Error(text);
      await delay(100);
    }
    throw Error('浏览器后端测试超时');
  } finally { socket?.close(); browser?.kill(); server.closeAllConnections(); server.close(); }
})().catch(error => { console.error(error); process.exitCode=1; });
