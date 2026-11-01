// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
// 仅供浏览器文件后端链接使用；不安装为公共 JS API。
addToLibrary({
  $GneissFetchStreams: {
    entries: new Map(),
    next: 1,
    close(id) {
      const stream = this.entries.get(id);
      if (!stream) return;
      this.entries.delete(id);
      stream.closed = true;
      stream.controller.abort();
      if (stream.reader) stream.reader.cancel().catch(() => {});
      if (stream.finish) stream.finish(4, 0, false);
    }
  },
  gneiss_fetch_stream_open__deps: ['$GneissFetchStreams', '$UTF8ToString'],
  gneiss_fetch_stream_open: function(url, context, done) {
    let settled = false;
    const controller = new AbortController();
    const timer = setTimeout(() => { controller.abort(); finish(1, 0); }, 30000);
    const finish = (status, id) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      {{{ makeDynCall('viii', 'done') }}}(context, status, id);
    };
    try {
      fetch(UTF8ToString(url), {signal: controller.signal}).then(response => {
        if (settled) { controller.abort(); return; }
        if (!response.ok) {
          controller.abort();
          finish(response.status === 404 ? 2 : 1, 0);
          return;
        }
        let reader = null;
        if (response.body) {
          // BYOB 限定每次 JS 分配和读取的大小；不退回无限积累块的方案。
          try { reader = response.body.getReader({mode: 'byob'}); }
          catch (_) { controller.abort(); finish(3, 0); return; }
        }
        if (GneissFetchStreams.next >= 2147483647) {
          controller.abort(); finish(5, 0); return;
        }
        const id = GneissFetchStreams.next++;
        GneissFetchStreams.entries.set(id, {controller, reader, closed: false, finish: null});
        finish(0, id);
      }).catch(() => { controller.abort(); finish(1, 0); });
    } catch (_) { controller.abort(); finish(1, 0); }
  },
  gneiss_fetch_stream_read__deps: ['$GneissFetchStreams'],
  gneiss_fetch_stream_read: function(id, destination, maximum, context, done) {
    const stream = GneissFetchStreams.entries.get(id);
    if (!stream || stream.closed || stream.finish) {
      {{{ makeDynCall('viiii', 'done') }}}(context, 4, 0, 0);
      return;
    }
    let settled = false;
    const finish = (status, count, eof) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      stream.finish = null;
      {{{ makeDynCall('viiii', 'done') }}}(context, status, count, eof ? 1 : 0);
    };
    // 只限制在途读取；消费者暂停期间没有计时器或后台 read()。
    const timer = setTimeout(() => { finish(1, 0, false); GneissFetchStreams.close(id); }, 30000);
    stream.finish = finish;
    if (!stream.reader) { finish(0, 0, true); return; }
    try {
      stream.reader.read(new Uint8Array(maximum)).then(value => {
        if (settled) return;
        const count = value.value ? value.value.byteLength : 0;
        if (count > maximum || (!count && !value.done)) {
          finish(1, 0, false); GneissFetchStreams.close(id); return;
        }
        if (count) HEAPU8.set(value.value, destination);
        finish(0, count, value.done);
      }).catch(() => finish(stream.closed ? 4 : 1, 0, false));
    } catch (_) { finish(5, 0, false); GneissFetchStreams.close(id); }
  },
  gneiss_fetch_stream_close__deps: ['$GneissFetchStreams'],
  gneiss_fetch_stream_close: function(id) { GneissFetchStreams.close(id); }
});
