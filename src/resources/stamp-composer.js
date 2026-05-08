const editor = document.getElementById('message-body');
  /* Undo / Redo */
  document.addEventListener('keydown',(e)=>{
    if(e.ctrlKey && e.key==='z' && !e.shiftKey){
      e.preventDefault();
      document.execCommand('undo');
    }

    if((e.ctrlKey && e.key==='y') || (e.ctrlKey && e.shiftKey && e.key==='Z')){
      e.preventDefault();
      document.execCommand('redo');
    }
  });

  /* Quote Handling */
  editor.addEventListener('keydown',(e)=>{
    if(e.key!=='Enter')return;

    /* Shift+Return stays in quote */
    if(e.shiftKey)return;

    const sel=window.getSelection();
    if(!sel.rangeCount)return;

    let node=sel.anchorNode;
    if(node.nodeType===3)node=node.parentNode;

    const p=node.closest('p');
    const quote=node.closest('blockquote');

    if(!p||!quote)return;

    e.preventDefault();

    const range=sel.getRangeAt(0);

    /* text before cursor */

    const before=range.cloneRange();
    before.selectNodeContents(p);
    before.setEnd(range.startContainer,range.startOffset);

    /* text after cursor */

    const after=range.cloneRange();
    after.selectNodeContents(p);
    after.setStart(range.startContainer,range.startOffset);

    const beforeFrag=before.extractContents();
    const afterFrag=after.extractContents();

    /* first part stays in quote */

    p.innerHTML='';
    p.appendChild(beforeFrag);

    /* new line */
    const newp=document.createElement('p');
    newp.innerHTML='<br>';

    /* second quote */
    const newQuote=document.createElement('blockquote');

    const p2=document.createElement('p');
    p2.appendChild(afterFrag);

    newQuote.appendChild(p2);

    /* move remaining lines */
    let next=p.nextSibling;
    while(next){
      let n=next;
      next=next.nextSibling;
      newQuote.appendChild(n);
    }

    quote.after(newp);
    newp.after(newQuote);

    /* set cursor */
    const r=document.createRange();
    r.setStart(newp,0);
    r.collapse(true);

    sel.removeAllRanges();
    sel.addRange(r);
});

