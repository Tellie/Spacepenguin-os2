#pragma once

// JavaScript features that newer sites expect but older QtWebEngine releases
// (QtWebEngine 6.2 = Chromium 94, as on OS/2) lack. SpacePenguin injects this
// into every page's main world on such builds; see Profiles::create().
// Each polyfill only fills a gap and leaves a native implementation alone.

#include <QString>

namespace spcompat {

inline QString polyfillScript()
{
    return QStringLiteral(R"JS(
(function () {
  function polyfills() {
    'use strict';
    var define = function (target, name, value) {
      if (target && !(name in target)) {
        Object.defineProperty(target, name, {
          value: value, writable: true, configurable: true, enumerable: false
        });
      }
    };

    // --- Arrays and typed arrays (Chromium 97, 110) ---------------------------
    var TypedArray = Object.getPrototypeOf(Int8Array.prototype);
    [Array.prototype, TypedArray].forEach(function (proto) {
      define(proto, 'findLast', function (fn, thisArg) {
        for (var i = this.length - 1; i >= 0; i--) {
          if (fn.call(thisArg, this[i], i, this)) return this[i];
        }
        return undefined;
      });
      define(proto, 'findLastIndex', function (fn, thisArg) {
        for (var i = this.length - 1; i >= 0; i--) {
          if (fn.call(thisArg, this[i], i, this)) return i;
        }
        return -1;
      });
      define(proto, 'toReversed', function () {
        return this.slice().reverse();
      });
      define(proto, 'toSorted', function (compare) {
        return this.slice().sort(compare);
      });
      define(proto, 'with', function (index, value) {
        var copy = this.slice();
        var i = Math.trunc(index) || 0;
        if (i < 0) i += copy.length;
        if (i < 0 || i >= copy.length) throw new RangeError('Invalid index');
        copy[i] = value;
        return copy;
      });
    });
    define(Array.prototype, 'toSpliced', function () {
      var copy = this.slice();
      copy.splice.apply(copy, arguments);
      return copy;
    });
    define(Array, 'fromAsync', async function (items, mapFn, thisArg) {
      var out = [];
      var i = 0;
      if (items && typeof items[Symbol.asyncIterator] === 'function') {
        for await (var v of items) out.push(mapFn ? await mapFn.call(thisArg, v, i++) : v);
      } else {
        for (var w of Array.from(items)) {
          w = await w;
          out.push(mapFn ? await mapFn.call(thisArg, w, i++) : w);
        }
      }
      return out;
    });

    // --- Grouping (Chromium 117) ------------------------------------------------
    define(Object, 'groupBy', function (items, fn) {
      var out = Object.create(null);
      var i = 0;
      for (var item of items) {
        var key = fn(item, i++);
        (out[key] || (out[key] = [])).push(item);
      }
      return out;
    });
    define(Map, 'groupBy', function (items, fn) {
      var out = new Map();
      var i = 0;
      for (var item of items) {
        var key = fn(item, i++);
        if (!out.has(key)) out.set(key, []);
        out.get(key).push(item);
      }
      return out;
    });

    // --- Promise.withResolvers (Chromium 119) -----------------------------------
    define(Promise, 'withResolvers', function () {
      var resolve, reject;
      var promise = new this(function (res, rej) { resolve = res; reject = rej; });
      return { promise: promise, resolve: resolve, reject: reject };
    });

    // --- Set methods (Chromium 122) ---------------------------------------------
    var other = function (s) {
      if (s instanceof Set) return s;
      // A set-like object (size/has/keys), e.g. a Map; anything else as an iterable.
      if (s && typeof s.has === 'function' && typeof s.keys === 'function') return new Set(s.keys());
      return new Set(s);
    };
    define(Set.prototype, 'union', function (s) {
      var out = new Set(this); other(s).forEach(function (v) { out.add(v); }); return out;
    });
    define(Set.prototype, 'intersection', function (s) {
      var o = other(s); var out = new Set();
      this.forEach(function (v) { if (o.has(v)) out.add(v); }); return out;
    });
    define(Set.prototype, 'difference', function (s) {
      var o = other(s); var out = new Set();
      this.forEach(function (v) { if (!o.has(v)) out.add(v); }); return out;
    });
    define(Set.prototype, 'symmetricDifference', function (s) {
      var o = other(s); var out = new Set(this);
      o.forEach(function (v) { if (out.has(v)) out.delete(v); else out.add(v); }); return out;
    });
    define(Set.prototype, 'isSubsetOf', function (s) {
      var o = other(s); var ok = true;
      this.forEach(function (v) { if (!o.has(v)) ok = false; }); return ok;
    });
    define(Set.prototype, 'isSupersetOf', function (s) {
      var self = this; var ok = true;
      other(s).forEach(function (v) { if (!self.has(v)) ok = false; }); return ok;
    });
    define(Set.prototype, 'isDisjointFrom', function (s) {
      var o = other(s); var ok = true;
      this.forEach(function (v) { if (o.has(v)) ok = false; }); return ok;
    });

    // --- Strings (Chromium 111) -------------------------------------------------
    var loneSurrogate = /[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(^|[^\uD800-\uDBFF])[\uDC00-\uDFFF]/;
    define(String.prototype, 'isWellFormed', function () {
      return !loneSurrogate.test(String(this));
    });
    define(String.prototype, 'toWellFormed', function () {
      return String(this).replace(/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(^|[^\uD800-\uDBFF])[\uDC00-\uDFFF]/g,
        function (m, before) { return (before || '') + '�'; });
    });

    // --- structuredClone (Chromium 98) ------------------------------------------
    define(self, 'structuredClone', function (value) {
      var seen = new Map();
      var clone = function (v) {
        if (v === null || typeof v !== 'object') {
          if (typeof v === 'function' || typeof v === 'symbol') {
            throw new DOMException('could not be cloned', 'DataCloneError');
          }
          return v;
        }
        if (seen.has(v)) return seen.get(v);
        var out;
        if (v instanceof Date) out = new Date(v.getTime());
        else if (v instanceof RegExp) out = new RegExp(v.source, v.flags);
        else if (v instanceof ArrayBuffer) out = v.slice(0);
        else if (ArrayBuffer.isView(v)) out = new v.constructor(v.buffer.slice(0), v.byteOffset, v.length);
        else if ((typeof Blob !== 'undefined' && v instanceof Blob) ||
                 (typeof ImageData !== 'undefined' && v instanceof ImageData)) out = v;
        else if (v instanceof Map) {
          out = new Map(); seen.set(v, out);
          v.forEach(function (val, key) { out.set(clone(key), clone(val)); });
          return out;
        } else if (v instanceof Set) {
          out = new Set(); seen.set(v, out);
          v.forEach(function (val) { out.add(clone(val)); });
          return out;
        } else if (v instanceof Error) {
          out = new v.constructor(v.message);
          out.stack = v.stack;
        } else if (Array.isArray(v)) {
          out = new Array(v.length); seen.set(v, out);
          for (var i = 0; i < v.length; i++) out[i] = clone(v[i]);
          return out;
        } else {
          out = {}; seen.set(v, out);
          Object.keys(v).forEach(function (k) { out[k] = clone(v[k]); });
          return out;
        }
        seen.set(v, out);
        return out;
      };
      return clone(value);
    });

    // --- Web APIs (Chromium 103-120) --------------------------------------------
    if (typeof AbortSignal !== 'undefined') {
      define(AbortSignal, 'timeout', function (ms) {
        var controller = new AbortController();
        setTimeout(function () {
          controller.abort(new DOMException('signal timed out', 'TimeoutError'));
        }, ms);
        return controller.signal;
      });
      define(AbortSignal, 'any', function (signals) {
        var controller = new AbortController();
        for (var s of signals) {
          if (s.aborted) { controller.abort(s.reason); return controller.signal; }
        }
        for (var t of signals) {
          t.addEventListener('abort', function (e) { controller.abort(e.target.reason); }, { once: true });
        }
        return controller.signal;
      });
      define(AbortSignal.prototype, 'throwIfAborted', function () {
        if (this.aborted) throw this.reason;
      });
    }
    if (typeof URL !== 'undefined') {
      define(URL, 'canParse', function (url, base) {
        try { new URL(url, base); return true; } catch (e) { return false; }
      });
    }
    if (typeof Response !== 'undefined') {
      define(Response, 'json', function (data, init) {
        init = Object.assign({}, init);
        var headers = new Headers(init.headers);
        if (!headers.has('content-type')) headers.set('content-type', 'application/json');
        init.headers = headers;
        return new Response(JSON.stringify(data), init);
      });
    }
    if (typeof Element !== 'undefined') {
      define(Element.prototype, 'checkVisibility', function () {
        if (!this.isConnected) return false;
        var style = getComputedStyle(this);
        if (style.display === 'none' || style.visibility === 'hidden') return false;
        return this.getClientRects().length > 0;
      });
    }

    // --- The :modal pseudo-class (Chromium 105) ---------------------------------
    // GitHub and others use selectors such as '[open]:not(:modal)'; Chromium 94
    // throws a SyntaxError on them. Mark dialogs opened with showModal() and
    // rewrite ':modal' to that marker in the selector APIs.
    var modalSupported = true;
    try { document.createDocumentFragment().querySelector(':modal'); } catch (e) { modalSupported = false; }
    if (!modalSupported && typeof HTMLDialogElement !== 'undefined') {
      var MARK = 'data-sp-modal';
      var dialogProto = HTMLDialogElement.prototype;
      var showModal = dialogProto.showModal;
      dialogProto.showModal = function () {
        var result = showModal.apply(this, arguments);
        this.setAttribute(MARK, '');
        var dialog = this;
        this.addEventListener('close', function () { dialog.removeAttribute(MARK); }, { once: true });
        return result;
      };
      var close = dialogProto.close;
      dialogProto.close = function () {
        this.removeAttribute(MARK);
        return close.apply(this, arguments);
      };
      var rewrite = function (selector) {
        return typeof selector === 'string' && selector.indexOf(':modal') >= 0
          ? selector.replace(/:modal\b/g, '[' + MARK + ']') : selector;
      };
      var wrap = function (proto, name) {
        var original = proto && proto[name];
        if (typeof original !== 'function') return;
        proto[name] = function (selector) {
          arguments[0] = rewrite(selector);
          return original.apply(this, arguments);
        };
      };
      [Element.prototype, Document.prototype, DocumentFragment.prototype].forEach(function (proto) {
        wrap(proto, 'querySelector');
        wrap(proto, 'querySelectorAll');
      });
      wrap(Element.prototype, 'matches');
      wrap(Element.prototype, 'closest');
      wrap(Element.prototype, 'webkitMatchesSelector');
    }

    // --- Anubis bot check: use its pure-JS hashing worker ----------------------
    // Anubis (e.g. on src.fedoraproject.org) hashes with crypto.subtle.digest in
    // a secure context. Each call is a round trip to another thread, which on
    // this QtWebEngine gives well under 1k hashes per second. Its pure-JS worker,
    // which Anubis itself uses on Firefox, is much faster here.
    if (typeof Worker === 'function') {
      var OrigWorker = Worker;
      var anubisWebCrypto = /(\/\.within\.website\/x\/cmd\/anubis\/static\/js\/worker\/sha256-)webcrypto(\.mjs)/;
      var PatchedWorker = function Worker(url, options) {
        if (!(this instanceof PatchedWorker)) return OrigWorker(url, options);
        var target = String(url);
        if (anubisWebCrypto.test(target)) target = target.replace(anubisWebCrypto, '$1purejs$2');
        return new OrigWorker(target, options);
      };
      PatchedWorker.prototype = OrigWorker.prototype;
      Object.setPrototypeOf(PatchedWorker, OrigWorker);
      self.Worker = PatchedWorker;
    }
  }

  polyfills();
})();
)JS");
}

} // namespace spcompat
