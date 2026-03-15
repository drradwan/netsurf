/* Polyfiller for QuickJS for NetSurf
 *
 * QuickJS-specific fork of duktape/polyfill.js.
 * Fixes:
 *   - typeof guards for DOMTokenList/DOMSettableTokenList
 *     (may not be defined yet when polyfill runs before DOM bindings)
 *
 * This JavaScript will be loaded into heaps before the generics.
 * We only care for the side-effects of this, be careful.
 */

// Production steps of ECMA-262, Edition 6, 22.1.2.1
if (!Array.from) {
  Array.from = (function () {
    var toStr = Object.prototype.toString;
    var isCallable = function (fn) {
      return typeof fn === 'function' || toStr.call(fn) === '[object Function]';
    };
    var toInteger = function (value) {
      var number = Number(value);
      if (isNaN(number)) { return 0; }
      if (number === 0 || !isFinite(number)) { return number; }
      return (number > 0 ? 1 : -1) * Math.floor(Math.abs(number));
    };
    var maxSafeInteger = Math.pow(2, 53) - 1;
    var toLength = function (value) {
      var len = toInteger(value);
      return Math.min(Math.max(len, 0), maxSafeInteger);
    };

    // The length property of the from method is 1.
    return function from(arrayLike/*, mapFn, thisArg */) {
      // 1. Let C be the this value.
      var C = this;

      // 2. Let items be ToObject(arrayLike).
      var items = Object(arrayLike);

      // 3. ReturnIfAbrupt(items).
      if (arrayLike == null) {
        throw new TypeError('Array.from requires an array-like object - not null or undefined');
      }

      // 4. If mapfn is undefined, then let mapping be false.
      var mapFn = arguments.length > 1 ? arguments[1] : void undefined;
      var T;
      if (typeof mapFn !== 'undefined') {
        // 5. else
        // 5. a If IsCallable(mapfn) is false, throw a TypeError exception.
        if (!isCallable(mapFn)) {
          throw new TypeError('Array.from: when provided, the second argument must be a function');
        }

        // 5. b. If thisArg was supplied, let T be thisArg; else let T be undefined.
        if (arguments.length > 2) {
          T = arguments[2];
        }
      }

      // 10. Let lenValue be Get(items, "length").
      // 11. Let len be ToLength(lenValue).
      var len = toLength(items.length);

      // 13. If IsConstructor(C) is true, then
      // 13. a. Let A be the result of calling the [[Construct]] internal method
      // of C with an argument list containing the single item len.
      // 14. a. Else, Let A be ArrayCreate(len).
      var A = isCallable(C) ? Object(new C(len)) : new Array(len);

      // 16. Let k be 0.
      var k = 0;
      // 17. Repeat, while k < len… (also steps a - h)
      var kValue;
      while (k < len) {
        kValue = items[k];
        if (mapFn) {
          A[k] = typeof T === 'undefined' ? mapFn(kValue, k) : mapFn.call(T, kValue, k);
        } else {
          A[k] = kValue;
        }
        k += 1;
      }
      // 18. Let putStatus be Put(A, "length", len, true).
      A.length = len;
      // 20. Return A.
      return A;
    };
  }());
}

// DOMTokenList formatter — guarded for QuickJS where DOM bindings
// may not be registered yet when this polyfill runs.
if (typeof DOMTokenList !== 'undefined') {
  DOMTokenList.prototype.toString = function () {
    if (this.length == 0) {
      return "";
    }

    var ret = this.item(0);
    for (var index = 1; index < this.length; index++) {
      ret = ret + " " + this.item(index);
    }

    return ret;
  };
}

// Inherit the same toString for settable lists
if (typeof DOMSettableTokenList !== 'undefined' && typeof DOMTokenList !== 'undefined') {
  DOMSettableTokenList.prototype.toString = DOMTokenList.prototype.toString;
}

// ═══ NodeIterator polyfill (DOM Traversal §1.2) ═══
(function(global) {
  'use strict';
  if (global.NodeIterator) return;

  var SHOW_ALL = 0xFFFFFFFF;
  var FILTER_ACCEPT = 1, FILTER_REJECT = 2, FILTER_SKIP = 3;

  function nodeTypeFlag(nodeType) {
    return 1 << (nodeType - 1);
  }

  function acceptNode(it, node) {
    if (!((nodeTypeFlag(node.nodeType) & it._whatToShow) || it._whatToShow === SHOW_ALL))
      return FILTER_SKIP;
    if (!it._filter) return FILTER_ACCEPT;
    var r;
    if (typeof it._filter === 'function') r = it._filter(node);
    else if (typeof it._filter.acceptNode === 'function') r = it._filter.acceptNode(node);
    else return FILTER_ACCEPT;
    return Number(r); /* spec: ToNumber coercion (true → 1 = FILTER_ACCEPT) */
  }

  function nextInDocOrder(node, root) {
    if (node.firstChild) return node.firstChild;
    var n = node;
    while (n && n !== root) {
      if (n.nextSibling) return n.nextSibling;
      n = n.parentNode;
    }
    return null;
  }

  function prevInDocOrder(node, root) {
    if (node === root) return null;
    if (node.previousSibling) {
      var n = node.previousSibling;
      while (n.lastChild) n = n.lastChild;
      return n;
    }
    return node.parentNode;
  }

  function NodeIterator(root, whatToShow, filter) {
    this.root = root;
    this._whatToShow = whatToShow || SHOW_ALL;
    this._filter = filter || null;
    this.referenceNode = root;
    this.pointerBeforeReferenceNode = true;
  }

  NodeIterator.prototype.nextNode = function() {
    var node = this.referenceNode;
    var beforeRef = this.pointerBeforeReferenceNode;
    while (true) {
      if (!beforeRef) {
        node = nextInDocOrder(node, this.root);
      }
      beforeRef = false;
      if (node === null) return null;
      var r = acceptNode(this, node);
      if (r === FILTER_ACCEPT) {
        this.referenceNode = node;
        this.pointerBeforeReferenceNode = false;
        return node;
      }
    }
  };

  NodeIterator.prototype.previousNode = function() {
    var node = this.referenceNode;
    var beforeRef = this.pointerBeforeReferenceNode;
    while (true) {
      if (beforeRef) {
        node = prevInDocOrder(node, this.root);
      }
      beforeRef = true;
      if (node === null) return null;
      var r = acceptNode(this, node);
      if (r === FILTER_ACCEPT) {
        this.referenceNode = node;
        this.pointerBeforeReferenceNode = true;
        return node;
      }
    }
  };

  NodeIterator.prototype.detach = function() {};

  // ═══ TreeWalker polyfill (DOM Traversal §1.3) ═══
  function TreeWalker(root, whatToShow, filter) {
    this.root = root;
    this._whatToShow = whatToShow || SHOW_ALL;
    this._filter = filter || null;
    this.currentNode = root;
  }

  TreeWalker.prototype.parentNode = function() {
    var node = this.currentNode;
    while (node && node !== this.root) {
      node = node.parentNode;
      if (node === null) return null;
      if (acceptNode(this, node) === FILTER_ACCEPT) {
        this.currentNode = node;
        return node;
      }
    }
    return null;
  };

  TreeWalker.prototype.firstChild = function() {
    return this._traverseChildren('first');
  };

  TreeWalker.prototype.lastChild = function() {
    return this._traverseChildren('last');
  };

  TreeWalker.prototype.nextSibling = function() {
    return this._traverseSiblings('next');
  };

  TreeWalker.prototype.previousSibling = function() {
    return this._traverseSiblings('previous');
  };

  TreeWalker.prototype.nextNode = function() {
    var node = this.currentNode;
    var r = FILTER_ACCEPT;
    while (true) {
      while (r !== FILTER_REJECT && node.firstChild !== null) {
        node = node.firstChild;
        r = acceptNode(this, node);
        if (r === FILTER_ACCEPT) { this.currentNode = node; return node; }
      }
      var sib = null, tmp = node;
      while (tmp !== null && tmp !== this.root) {
        sib = tmp.nextSibling;
        if (sib !== null) break;
        tmp = tmp.parentNode;
      }
      if (sib === null || tmp === null || tmp === this.root) return null;
      node = sib;
      r = acceptNode(this, node);
      if (r === FILTER_ACCEPT) { this.currentNode = node; return node; }
    }
  };

  TreeWalker.prototype.previousNode = function() {
    var node = this.currentNode;
    while (node !== this.root) {
      var sib = node.previousSibling;
      while (sib !== null) {
        node = sib;
        var r = acceptNode(this, node);
        while (r !== FILTER_REJECT && node.lastChild !== null) {
          node = node.lastChild;
          r = acceptNode(this, node);
        }
        if (r === FILTER_ACCEPT) { this.currentNode = node; return node; }
        sib = node.previousSibling;
      }
      if (node === this.root || node.parentNode === null) return null;
      node = node.parentNode;
      if (acceptNode(this, node) === FILTER_ACCEPT) {
        this.currentNode = node;
        return node;
      }
    }
    return null;
  };

  TreeWalker.prototype._traverseChildren = function(type) {
    var node = (type === 'first') ? this.currentNode.firstChild : this.currentNode.lastChild;
    while (node !== null) {
      var r = acceptNode(this, node);
      if (r === FILTER_ACCEPT) { this.currentNode = node; return node; }
      if (r === FILTER_SKIP) {
        var child = (type === 'first') ? node.firstChild : node.lastChild;
        if (child !== null) { node = child; continue; }
      }
      while (node !== null) {
        var sib = (type === 'first') ? node.nextSibling : node.previousSibling;
        if (sib !== null) { node = sib; break; }
        var parent = node.parentNode;
        if (parent === null || parent === this.root || parent === this.currentNode) { return null; }
        node = parent;
      }
    }
    return null;
  };

  TreeWalker.prototype._traverseSiblings = function(type) {
    var node = this.currentNode;
    if (node === this.root) return null;
    while (true) {
      var sib = (type === 'next') ? node.nextSibling : node.previousSibling;
      while (sib !== null) {
        node = sib;
        var r = acceptNode(this, node);
        if (r === FILTER_ACCEPT) { this.currentNode = node; return node; }
        if (r === FILTER_SKIP) {
          /* Per DOM spec: try children of skipped node first,
           * then fall back to its next/previous sibling */
          var child = (type === 'next') ? node.firstChild : node.lastChild;
          if (child !== null) {
            sib = child;
          } else {
            sib = (type === 'next') ? node.nextSibling : node.previousSibling;
          }
        } else {
          sib = (type === 'next') ? node.nextSibling : node.previousSibling;
        }
      }
      node = node.parentNode;
      if (node === null || node === this.root) return null;
      if (acceptNode(this, node) === FILTER_ACCEPT) return null;
    }
  };

  global.NodeIterator = NodeIterator;
  global.TreeWalker = TreeWalker;

  /* Patch document prototype — use __proto__ since Document constructor
   * may not be a global in nsgenbind's QuickJS binding system. */
  global.__NSQJS_patchDoc_traversal = function(doc) {
    if (!doc) return;
    var proto = Object.getPrototypeOf(doc);
    if (proto) {
      proto.createNodeIterator = function(root, whatToShow, filter) {
        return new NodeIterator(root, whatToShow, filter);
      };
      proto.createTreeWalker = function(root, whatToShow, filter) {
        return new TreeWalker(root, whatToShow, filter);
      };
    }
  };
})(this);

// ═══ Range polyfill (DOM Range §2) ═══
(function(global) {
  'use strict';
  /* Override native Range — nsgenbind generates a stub Range class with
   * empty setStart/setEnd methods that don't call libdom. Our polyfill
   * provides a complete JS implementation with proper collapse behavior. */

  var _activeRanges = [];

  function _isDescendant(node, ancestor) {
    for (var n = node; n; n = n.parentNode) if (n === ancestor) return true;
    return false;
  }

  function _indexOf(node) {
    var i = 0, n = node.parentNode.firstChild;
    while (n && n !== node) { i++; n = n.nextSibling; }
    return i;
  }

  /* DOM spec: when a node is removed, update all live ranges whose
   * boundaries reference the removed node or its descendants. */
  function _rangePreRemove(child, parent) {
    var idx = _indexOf(child);
    for (var i = 0; i < _activeRanges.length; i++) {
      var r = _activeRanges[i];
      /* If startContainer is inside the removed subtree, collapse to parent */
      if (_isDescendant(r.startContainer, child)) {
        r.startContainer = parent;
        r.startOffset = idx;
      } else if (r.startContainer === parent && r.startOffset > idx) {
        r.startOffset--;
      }
      if (_isDescendant(r.endContainer, child)) {
        r.endContainer = parent;
        r.endOffset = idx;
      } else if (r.endContainer === parent && r.endOffset > idx) {
        r.endOffset--;
      }
      r._update();
    }
  }

  function Range() {
    this.startContainer = null;
    this.startOffset = 0;
    this.endContainer = null;
    this.endOffset = 0;
    this.collapsed = true;
    this.commonAncestorContainer = null;
    _activeRanges.push(this);
  }

  Range.START_TO_START = 0;
  Range.START_TO_END = 1;
  Range.END_TO_END = 2;
  Range.END_TO_START = 3;

  Range.prototype.setStart = function(node, offset) {
    this.startContainer = node;
    this.startOffset = offset;
    /* DOM spec: if start is after end, collapse end to start */
    if (this.endContainer !== null && this._comparePosition(node, offset, this.endContainer, this.endOffset) > 0) {
      this.endContainer = node; this.endOffset = offset;
    }
    this._update();
  };

  Range.prototype.setEnd = function(node, offset) {
    this.endContainer = node;
    this.endOffset = offset;
    /* DOM spec: if end is before start, collapse start to end */
    if (this.startContainer !== null && this._comparePosition(this.startContainer, this.startOffset, node, offset) > 0) {
      this.startContainer = node; this.startOffset = offset;
    }
    this._update();
  };

  Range.prototype.setStartBefore = function(node) {
    this.setStart(node.parentNode, this._indexOf(node));
  };

  Range.prototype.setStartAfter = function(node) {
    this.setStart(node.parentNode, this._indexOf(node) + 1);
  };

  Range.prototype.setEndBefore = function(node) {
    this.setEnd(node.parentNode, this._indexOf(node));
  };

  Range.prototype.setEndAfter = function(node) {
    this.setEnd(node.parentNode, this._indexOf(node) + 1);
  };

  Range.prototype.collapse = function(toStart) {
    if (toStart) {
      this.endContainer = this.startContainer;
      this.endOffset = this.startOffset;
    } else {
      this.startContainer = this.endContainer;
      this.startOffset = this.endOffset;
    }
    this.collapsed = true;
  };

  Range.prototype.selectNode = function(node) {
    this.setStartBefore(node);
    this.setEndAfter(node);
  };

  Range.prototype.selectNodeContents = function(node) {
    this.startContainer = node;
    this.startOffset = 0;
    this.endContainer = node;
    this.endOffset = node.childNodes ? node.childNodes.length : 0;
    this._update();
  };

  Range.prototype.cloneRange = function() {
    var r = new Range();
    r.startContainer = this.startContainer;
    r.startOffset = this.startOffset;
    r.endContainer = this.endContainer;
    r.endOffset = this.endOffset;
    r._update();
    return r;
  };

  Range.prototype.cloneContents = function() {
    var doc = this.startContainer.ownerDocument || this.startContainer;
    var frag = doc.createDocumentFragment();
    if (this.collapsed) return frag;
    if (this.startContainer === this.endContainer && this.startContainer.nodeType === 3) {
      var t = this.startContainer.cloneNode(false);
      t.data = this.startContainer.data.substring(this.startOffset, this.endOffset);
      frag.appendChild(t);
      return frag;
    }
    var nodes = this._getContainedNodes();
    for (var i = 0; i < nodes.length; i++) {
      frag.appendChild(nodes[i].cloneNode(true));
    }
    return frag;
  };

  Range.prototype.extractContents = function() {
    var doc = this.startContainer.ownerDocument || this.startContainer;
    var frag = doc.createDocumentFragment();
    if (this.collapsed) return frag;

    /* Same container — simple case */
    if (this.startContainer === this.endContainer) {
      if (this.startContainer.nodeType === 3) {
        var clone = this.startContainer.cloneNode(false);
        clone.data = this.startContainer.data.substring(this.startOffset, this.endOffset);
        this.startContainer.deleteData(this.startOffset, this.endOffset - this.startOffset);
        frag.appendChild(clone);
      } else {
        var cn = this.startContainer.childNodes;
        var toMove = [];
        for (var i = this.startOffset; i < this.endOffset && i < cn.length; i++)
          toMove.push(cn[i]);
        for (var i = 0; i < toMove.length; i++) frag.appendChild(toMove[i]);
      }
      this.collapse(true);
      return frag;
    }

    /* Cross-container: build ancestor chains to common ancestor */
    var ca = this.commonAncestorContainer;
    var self = this;

    function chainTo(node, ancestor) {
      var c = [];
      for (var n = node; n && n !== ancestor; n = n.parentNode) c.push(n);
      return c;
    }

    var startChain = chainTo(this.startContainer, ca);
    var endChain = chainTo(this.endContainer, ca);
    var startChild = startChain.length > 0 ? startChain[startChain.length - 1] : null;
    var endChild = endChain.length > 0 ? endChain[endChain.length - 1] : null;

    /* Extract content AFTER start point, cloning ancestor path */
    var startFrag = null;
    if (startChild) {
      var node = startChain[0]; /* startContainer */
      var extracted;
      if (node.nodeType === 3) {
        extracted = node.splitText(self.startOffset);
      } else {
        extracted = doc.createDocumentFragment();
        var cn = node.childNodes, tm = [];
        for (var i = self.startOffset; i < cn.length; i++) tm.push(cn[i]);
        for (var i = 0; i < tm.length; i++) extracted.appendChild(tm[i]);
      }
      for (var i = 1; i < startChain.length; i++) {
        var anc = startChain[i];
        var cl = anc.cloneNode(false);
        cl.appendChild(extracted);
        var sib = startChain[i-1].nextSibling;
        while (sib) { var nx = sib.nextSibling; cl.appendChild(sib); sib = nx; }
        extracted = cl;
      }
      startFrag = extracted;
    }

    /* Collect fully-contained direct children of ca between start and end */
    var fullyContained = [];
    if (startChild && endChild) {
      var n = startChild.nextSibling;
      while (n && n !== endChild) { fullyContained.push(n); n = n.nextSibling; }
    }

    /* Extract content BEFORE end point, cloning ancestor path */
    var endFrag = null;
    if (endChild && endChild !== startChild) {
      var node = endChain[0]; /* endContainer */
      var extracted;
      if (node.nodeType === 3) {
        var clone = node.cloneNode(false);
        clone.data = node.data.substring(0, self.endOffset);
        node.data = node.data.substring(self.endOffset);
        extracted = clone;
      } else {
        var cl = node.cloneNode(false);
        var cn = node.childNodes, tm = [];
        for (var i = 0; i < self.endOffset && i < cn.length; i++) tm.push(cn[i]);
        for (var i = 0; i < tm.length; i++) cl.appendChild(tm[i]);
        extracted = cl;
      }
      for (var i = 1; i < endChain.length; i++) {
        var anc = endChain[i];
        var cl = anc.cloneNode(false);
        var sib = anc.firstChild;
        while (sib && sib !== endChain[i-1]) {
          var nx = sib.nextSibling; cl.appendChild(sib); sib = nx;
        }
        cl.appendChild(extracted);
        extracted = cl;
      }
      endFrag = extracted;
    }

    /* Assemble fragment */
    if (startFrag) frag.appendChild(startFrag);
    for (var i = 0; i < fullyContained.length; i++) frag.appendChild(fullyContained[i]);
    if (endFrag) frag.appendChild(endFrag);

    this.collapse(true);
    return frag;
  };

  Range.prototype.deleteContents = function() {
    if (this.collapsed) return;
    var nodes = this._getContainedNodes();
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].parentNode) nodes[i].parentNode.removeChild(nodes[i]);
    }
    this.collapse(true);
  };

  Range.prototype.insertNode = function(node) {
    if (this.startContainer.nodeType === 3) {
      var text = this.startContainer;
      var so = this.startOffset;
      var sameContainer = (this.endContainer === text);
      var eo = this.endOffset;
      var newText = text.splitText(so);
      /* Adjust end boundary per splitText spec: if end was in the same
       * text node past the split point, move it to the new text node */
      if (sameContainer && eo > so) {
        this.endContainer = newText;
        this.endOffset = eo - so;
      }
      text.parentNode.insertBefore(node, newText);
      this._update();
    } else {
      var ref = this.startContainer.childNodes[this.startOffset] || null;
      this.startContainer.insertBefore(node, ref);
    }
  };

  Range.prototype.surroundContents = function(newParent) {
    /* Check for partial selection of a non-text node */
    if (this.startContainer !== this.endContainer) {
      var sc = this.startContainer, ec = this.endContainer;
      if (sc.nodeType !== 3 && sc.nodeType !== 8) {
        var e = {message: 'InvalidStateError', code: 11, HIERARCHY_REQUEST_ERR: 3, INVALID_STATE_ERR: 11};
        throw e;
      }
      if (ec.nodeType !== 3 && ec.nodeType !== 8) {
        var e = {message: 'InvalidStateError', code: 11, HIERARCHY_REQUEST_ERR: 3, INVALID_STATE_ERR: 11};
        throw e;
      }
    }
    /* Check hierarchy: newParent can't be inserted into Document with existing children */
    var container = this.startContainer;
    if (container.nodeType === 9 /* Document */ && container.childNodes.length > 0) {
      var e = {message: 'HierarchyRequestError', code: 3, HIERARCHY_REQUEST_ERR: 3};
      throw e;
    }
    var contents = this.extractContents();
    this.insertNode(newParent);
    newParent.appendChild(contents);
    this.selectNode(newParent);
  };

  Range.prototype.compareBoundaryPoints = function(how, sourceRange) {
    var sc, so, ec, eo;
    switch (how) {
      case Range.START_TO_START: sc = this.startContainer; so = this.startOffset; ec = sourceRange.startContainer; eo = sourceRange.startOffset; break;
      case Range.START_TO_END: sc = this.endContainer; so = this.endOffset; ec = sourceRange.startContainer; eo = sourceRange.startOffset; break;
      case Range.END_TO_END: sc = this.endContainer; so = this.endOffset; ec = sourceRange.endContainer; eo = sourceRange.endOffset; break;
      case Range.END_TO_START: sc = this.startContainer; so = this.startOffset; ec = sourceRange.endContainer; eo = sourceRange.endOffset; break;
    }
    if (sc === ec) return so < eo ? -1 : so > eo ? 1 : 0;
    var pos = sc.compareDocumentPosition ? sc.compareDocumentPosition(ec) : 0;
    if (pos & 4) return -1;
    if (pos & 2) return 1;
    return 0;
  };

  Range.prototype.toString = function() {
    if (this.collapsed) return '';
    if (this.startContainer === this.endContainer && this.startContainer.nodeType === 3) {
      return this.startContainer.data.substring(this.startOffset, this.endOffset);
    }
    var s = '';
    /* Partial start text */
    if (this.startContainer.nodeType === 3) {
      s += this.startContainer.data.substring(this.startOffset);
    }
    /* Walk tree from start to end, collecting text of fully-contained nodes */
    var end = this.endContainer;
    function nextInDoc(n) {
      if (n.firstChild) return n.firstChild;
      while (n) { if (n.nextSibling) return n.nextSibling; n = n.parentNode; }
      return null;
    }
    function isAncestor(a, d) {
      for (var n = d; n; n = n.parentNode) if (n === a) return true;
      return false;
    }
    var cur;
    if (this.startContainer.nodeType === 3) {
      cur = nextInDoc(this.startContainer);
    } else {
      cur = this.startContainer.childNodes[this.startOffset] || nextInDoc(this.startContainer);
    }
    while (cur && cur !== end) {
      if (isAncestor(cur, end)) { cur = cur.firstChild; continue; }
      if (cur.nodeType === 3) s += cur.data;
      cur = nextInDoc(cur);
    }
    /* Partial end text */
    if (this.endContainer.nodeType === 3) {
      s += this.endContainer.data.substring(0, this.endOffset);
    }
    return s;
  };

  Range.prototype.detach = function() {
    var idx = _activeRanges.indexOf(this);
    if (idx >= 0) _activeRanges.splice(idx, 1);
  };

  Range.prototype._indexOf = function(node) {
    var i = 0, n = node.parentNode.firstChild;
    while (n && n !== node) { i++; n = n.nextSibling; }
    return i;
  };

  /* Compare two boundary points in document order. Returns -1, 0, or 1.
   * (container1, offset1) vs (container2, offset2) per DOM Range spec. */
  Range.prototype._comparePosition = function(c1, o1, c2, o2) {
    if (c1 === c2) return o1 < o2 ? -1 : (o1 > o2 ? 1 : 0);
    /* Build ancestor chains (root first) */
    var a1 = [], a2 = [], n;
    for (n = c1; n; n = n.parentNode) a1.unshift(n);
    for (n = c2; n; n = n.parentNode) a2.unshift(n);
    /* Find deepest common ancestor index */
    var i = 0;
    while (i < a1.length && i < a2.length && a1[i] === a2[i]) i++;
    if (i === 0) return 0; /* no common ancestor */
    /* If c1 is ancestor of c2: compare o1 with index of c2's path child */
    if (i === a1.length) {
      var idx2 = this._indexOf(a2[i]); /* index of c2's path under c1 */
      return o1 <= idx2 ? -1 : 1;
    }
    /* If c2 is ancestor of c1: compare index of c1's path child with o2 */
    if (i === a2.length) {
      var idx1 = this._indexOf(a1[i]);
      return idx1 < o2 ? -1 : 1;
    }
    /* Compare siblings under common ancestor */
    var s1 = a1[i], s2 = a2[i];
    for (n = s1; n; n = n.nextSibling) {
      if (n === s2) return -1;
    }
    return 1;
  };

  Range.prototype._update = function() {
    if (this.startContainer === null) { this.collapsed = true; return; }
    if (this.endContainer === null) { this.endContainer = this.startContainer; this.endOffset = this.startOffset; }
    this.collapsed = (this.startContainer === this.endContainer && this.startOffset === this.endOffset);
    if (this.startContainer === this.endContainer) {
      this.commonAncestorContainer = this.startContainer;
    } else {
      var a = this.startContainer, b = this.endContainer;
      var ap = [], bp = [];
      while (a) { ap.push(a); a = a.parentNode; }
      while (b) { bp.push(b); b = b.parentNode; }
      this.commonAncestorContainer = null;
      for (var i = 0; i < ap.length; i++) {
        for (var j = 0; j < bp.length; j++) {
          if (ap[i] === bp[j]) { this.commonAncestorContainer = ap[i]; return; }
        }
      }
    }
  };

  Range.prototype._getContainedNodes = function() {
    var nodes = [];
    if (this.collapsed || !this.startContainer) return nodes;
    var root = this.commonAncestorContainer;
    function walk(node) {
      if (!node) return;
      var child = node.firstChild;
      while (child) {
        var next = child.nextSibling;
        if (child !== root) {
          walk(child);
          if (child.nodeType !== 3) { /* element: add if fully contained */ }
        }
        child = next;
      }
    }
    if (this.startContainer === this.endContainer) {
      if (this.startContainer.nodeType === 3) return nodes;
      var cn = this.startContainer.childNodes;
      for (var i = this.startOffset; i < this.endOffset && i < cn.length; i++) {
        nodes.push(cn[i]);
      }
    } else {
      var cn2 = root.childNodes;
      if (cn2) {
        for (var k = 0; k < cn2.length; k++) {
          nodes.push(cn2[k]);
        }
      }
    }
    return nodes;
  };

  global.Range = Range;

  /* Expose pre-remove hook for removeChild wrapper */
  Range._preRemove = _rangePreRemove;

  global.__NSQJS_patchDoc_range = function(doc) {
    if (!doc) return;
    var proto = Object.getPrototypeOf(doc);
    if (proto) {
      proto.createRange = function() {
        var r = new Range();
        r.startContainer = this;
        r.startOffset = 0;
        r.endContainer = this;
        r.endOffset = 0;
        r.collapsed = true;
        r.commonAncestorContainer = this;
        return r;
      };
    }
  };
})(this);
