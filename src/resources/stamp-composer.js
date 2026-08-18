const editor = document.getElementById('message-body');

/* Undo / Redo */
document.addEventListener('keydown', (e) => {
  if (e.ctrlKey && e.key === 'z' && !e.shiftKey) {
    e.preventDefault();
    document.execCommand('undo');
  }

  if ((e.ctrlKey && e.key === 'y') || (e.ctrlKey && e.shiftKey && e.key === 'Z')) {
    e.preventDefault();
    document.execCommand('redo');
  }
});

document.body.addEventListener('keydown', (e) => {
  if (e.key !== 'Enter' || e.shiftKey) return;
  if (e.ctrlKey || e.metaKey || e.altKey) return;

  const sel = window.getSelection();
  if (!sel || !sel.rangeCount) return;

  let node = sel.anchorNode;
  if (!node) return;
  if (node.nodeType === 3) node = node.parentNode;

  let n = node;
  while (n && n !== document.body) {
    if (n.nodeName === 'BLOCKQUOTE') return;
    if (n.nodeName === 'LI' || n.nodeName === 'UL' || n.nodeName === 'OL') return;
    n = n.parentNode;
  }

  e.preventDefault();
  document.execCommand('insertLineBreak', false, null);
}, true);

document.body.addEventListener('keyup', (e) => {
  if (e.key !== 'Enter' || e.shiftKey) return;
  if (e.ctrlKey || e.metaKey || e.altKey) return;

  const sel = window.getSelection();
  if (!sel || !sel.rangeCount) return;

  let node = sel.anchorNode;
  if (!node) return;
  if (node.nodeType === 3) node = node.parentNode;

  /* Count how many blockquote ancestors we are inside */
  let depth = 0;
  let n = node;
  while (n && n !== document.body) {
    if (n.nodeName === 'BLOCKQUOTE') depth++;
    n = n.parentNode;
  }

  /* Outdent once per level to break out of all enclosing blockquotes */
  for (let i = 0; i < depth; i++) {
    document.execCommand('outdent', false, null);
  }
}, true);
