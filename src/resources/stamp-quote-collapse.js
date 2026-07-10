(function () {
  "use strict";

  if (document.body.isContentEditable)
    return;

  if (!document.getElementById("stamp-quote-style")) {
    var css = document.createElement("style");

    css.id = "stamp-quote-style";

    css.textContent =
      "blockquote.stamp-quote-collapsed {" +
        "max-height: 6em;" +
        "overflow: hidden;" +
        "position: relative;" +
        "background: #e0e0e0" +
      "}" +
      "blockquote.stamp-quote-collapsed::after {" +
        "content: \"\";" +
        "position: absolute;" +
        "bottom: 0;" +
        "left: 0;" +
        "right: 0;" +
        "height: 1.5em;" +
        "background: linear-gradient(transparent,#e0e0e0);" +
        "pointer-events: none" +
      "}" +
      ".stamp-quote-expand-btn {" +
        "display: block;" +
        "width: calc(100% - 24px);" +
        "margin:0 12px;" +
        "padding:4px;" +
        "border:none;" +
        "border-top:1px solid #ccc;" +
        "background:#e0e0e0;" +
        "color:#666;" +
        "cursor:pointer;" +
        "font-size:16px;" +
        "text-align:center" +
      "}" +
      ".stamp-quote-expand-btn:hover {" +
        "background:#d0d0d0" +
      "}" +
      "@media(prefers-color-scheme:dark) {" +
        "blockquote.stamp-quote-collapsed {" +
          "background:#333 !important" +
        "}" +
        "blockquote.stamp-quote-collapsed::after {" +
          "background:linear-gradient(transparent,#333) !important" +
        "}" +
        ".stamp-quote-expand-btn {" +
          "border-top-color:#555;" +
          "background:#333;" +
          "color:#aaa" +
        "}" +
        ".stamp-quote-expand-btn:hover {" +
          "background:#444" +
        "}" +
      "}";
    document.head.appendChild(css);
  }

  var body = document.getElementById("message-body");
  if (body) {
    var parts = body.innerHTML.split(/(<br\s*\/?>)/i);
    var result = [];
    var buf = [];
    var quote = false;

    function flush() {
      if (buf.length) {
        result.push("<blockquote>" + buf.join("") + "</blockquote>");
        buf = [];
      }
    }

    for (var i = 0; i < parts.length; i++) {
      var p = parts[i];

      if (/^<br\s*\/?>$/i.test(p)) {
        quote ? buf.push(p) : result.push(p);
      } else if (/^\s*(?:&gt;|>)/.test(p.replace(/<[^>]*>/g, ""))) {
        if (!quote) {
          if (result.length && /^<br\s*\/?>$/i.test(result[result.length - 1]))
            buf.push(result.pop());

          quote = true;
        }

        buf.push(p);
      } else {
        flush();
        result.push(p);

        quote = false;
      }
    }

    flush();
    body.innerHTML = result.join("");
  }

  [].forEach.call(document.querySelectorAll("blockquote"), function (q) {
    if (q.classList.contains("stamp-quote-processed"))
      return;

    q.classList.add("stamp-quote-processed");

    if (q.scrollHeight <= 150)
      return;

    q.classList.add("stamp-quote-collapsed");

    var btn = document.createElement("button");
    btn.innerHTML = "&#9660;";
    btn.className = "stamp-quote-expand-btn";
    btn.addEventListener("click", function () {
      q.classList.remove("stamp-quote-collapsed");
      btn.remove();
      window.webkit.messageHandlers.stampResize.postMessage("");
    });

    q.parentNode.insertBefore(btn, q.nextSibling);
  });
})();
