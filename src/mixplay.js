/* MPCOMM_STAT == 0 - normal update
   MPCOMM_TITLES == 1 - +titles
   MPCOMM_RESULT == 2 - searchresult
   MPCOMM_LISTS == 4 - dnp/fav lists
   MPCOMM_CONFIG == 8 - configuration
   -1 - stopped */
var doUpdate = 13
var isstream = 0
var msglines = []
var msgpos = 0
var scrolls = []
var numscrolls = 0
var favplay = 0
var cmdtosend = ''
var argtosend = ''
var smallUI = 0
var active = 0
var swipest = []
var overflow = 0
var toval = 500
var idletime = 0
var idlesleep = 30000 /* milliseconds until the clock shows up (30s) */
var currentPop = ''
var debug = false
const layout = ['1234567890', 'qwertzuiop', 'asdfghjkl\'', 'yxcvbnm-', 'XC BO']
var kbddiv
var kbdokay = function () {}
var confokay = function () {}
var clientid = -1
var shortcuts = []
var curvol = 0
var inUpdate = 0

function debugLog (txt) {
  if (debug) {
    console.log(txt)
  }
}

function sendKey (key) {
  const e = document.getElementById('kbdtext')
  var text = e.value
  switch (key) {
    case 'B': // backspace
      if (text.length > 0) {
        e.value = text.substr(0, text.length - 1)
      }
      break
    case 'C': // clear
      e.value = ''
      break
    case 'O': // Okay
      kbddiv.className = 'hide'
      kbdokay()
      break
    case 'X': // cancel
      e.value = ''
      kbddiv.className = 'hide'
      break
    default:
      e.value = text + key
  }
}

function createKbdKey (name, key) {
  var btn = document.createElement('button')
  btn.innerHTML = name
  btn.className = 'kbdkey'
  btn.onclick = function () { sendKey(key) }

  if (key === ' ') {
    btn.className = 'kbdspace'
  } else if (key !== name) {
    btn.className = 'kbdfnkey'
  }
  return btn
}

function kbdRealkey (event) {
  if (event.keyCode === 13) {
    kbddiv.className = 'hide'
    kbdokay()
    return false
  }
  debugLog('Event is ' + event.keyCode)
  return true
}

function initKbdDiv () {
  var row, col, btnrow, btn, element
  const e = document.getElementById('textkbd')
  if (!e) {
    showConfirm('No keyboard div!')
  }
  wipeElements(e)
  element = document.createElement('label')
  element.innerHTML = 'Text: '
  element.id = 'kbdhead'
  btn = document.createElement('input')
  btn.type = 'text'
  btn.id = 'kbdtext'
  btn.addEventListener('keypress', kbdRealkey)
  element.appendChild(btn)
  e.appendChild(element)
  for (row = 0; row < layout.length; row++) {
    btnrow = document.createElement('div')
    btnrow.className = 'kbdrow'
    for (col = 0; col < layout[row].length; col++) {
      switch (layout[row].charAt(col)) {
        case 'B':
          btn = createKbdKey('<-', 'B')
          btnrow.appendChild(btn)
          break
        case 'C':
          btn = createKbdKey('CLR', 'C')
          break
        case 'O':
          btn = createKbdKey('&#x2714;', 'O')
          break
        case 'X':
          btn = createKbdKey('X', 'X')
          break
        case ' ':
          btn = createKbdKey('&nbsp;', ' ')
          break
        default:
          btn = createKbdKey(layout[row].charAt(col), layout[row].charAt(col))
      }
      btnrow.appendChild(btn)
    }
    e.appendChild(btnrow)
  }
  kbddiv = e
}

/*
 * default callback for showKbd
 * takes the entered text sends it to the server as an argument to cmd
 * if clear is set, the entered value is unset after evaluating it
 */
function sendArg (cmd) {
  const term = document.getElementById('kbdtext').value

  if (term.length > 1) {
    sendCMDArg(cmd, term)
  } else {
    showConfirm('Need at least two letters!')
  }
}

/*
 * show confirmation dialog with message msg
 * ok is the callback on confirmation:
 *  - undefined        : no cancel button, no callback on okay
 *  - <num>            : sendCMD(<num>) on okay
 *  - function() {...} : callback to invoke on okay
 */
function showConfirm (msg, ok) {
  const cdiv = document.getElementById('confirm')
  if (cdiv.className === 'hide') {
    document.getElementById('confmsg').innerHTML = msg
    cdiv.className = ''
    if (!ok) {
      confokay = function () { confHide() }
      enableElement('confcanc', 0)
    } else {
      if (ok > 0) {
        confokay = function () { sendCMD(ok) }
      } else {
        confokay = ok
      }
      enableElement('confcanc', 1)
    }
  } else {
    /* dialog is already open, add msg. */
    document.getElementById('confmsg').innerHTML += '<br/>' + msg
  }
}

function confOK () {
  confHide()
  confokay()
}

function confHide () {
  document.getElementById('confirm').className = 'hide'
}

/*
 * display on-screen keyboard and set callback on 'okay'
 */
function showKbd (ok) {
  /* the target textfield */
  if (!kbddiv) {
    showConfirm('Keyboard was not initialized!')
    return
  }
  /* the keyboard textfield */
  const t = document.getElementById('kbdtext')
  if (kbddiv.className === 'hide') {
    kbddiv.className = ''
    if (ok > 0) {
      kbdokay = function () { sendArg(ok) }
    } else {
      kbdokay = ok
    }
    t.focus()
  } else {
    debugLog('Keyboard already open!')
  }
}

function setBody (cname) {
  document.body.className = cname
}

function clearBody () {
  document.body.className = ''
}

function replaceChild (e, c) {
  wipeElements(e)
  e.appendChild(c)
}

/*
 * switches search mode and removes previous results
 */
function toggleSearch () {
  sendCMD(0x0019)
  var e = document.getElementById('search0')
  var text = document.createElement('em')
  text.innerHTML = 'No titles found!'
  replaceChild(e, text)
  e = document.getElementById('search1')
  text = document.createElement('em')
  text.innerHTML = 'No artists found!'
  replaceChild(e, text)
  e = document.getElementById('search2')
  text = document.createElement('em')
  text.innerHTML = 'No albums found!'
  replaceChild(e, text)
}

/*
 * check if a long text needs to be eolled and then scroll into the
 * proper direction
 */
function scrollToggle () {
  var to = 1000
  for (var i = 0; i < numscrolls; i++) {
    var element = scrolls[i].element
    if (scrolls[i].offset.charAt(0) === '-') {
      if (element.style.right === scrolls[i].offset) {
        element.style.right = '0px'
        to = 6000
      } else {
        element.style.right = scrolls[i].offset
        to = 11000
      }
    }
  }
  setTimeout(function () { scrollToggle() }, to)
}

function isActive (tab) {
  const el = document.getElementById(tab)
  return (el && (el.className === 'active'))
}

function isPlay () {
  return (isActive('extra0') ||
    ((isActive('extra1') && isActive('dnpfav0'))))
}

/*
 * Scales the font to fit into current window, enables scrolling
 * on texts that are longer than the parent containerand centers
 * shorter texts
 * if keep is set to 1, the current tab is not switched
 * if keep is -1 then the current tab is switched even when
 * currently not in a play view
 */
function adaptUI (keep) {
  /* Number of lines in sub-tabs */
  var lines
  var minfont = 12
  var maxfont = 30
  var h = window.innerHeight
  var i
  var fsize
  var bsize
  const portrait = (h > window.innerWidth * 1.3)

  /* cludge to keep stuff readable on non-aliasing displays */
  if (!portrait) {
    minfont = 18
  }

  const reqfs = Math.min(window.innerWidth / 24, window.innerHeight / 14) + 'px'
  document.getElementById('confirm').style.fontSize = reqfs
  if (kbddiv !== undefined) {
    kbddiv.style.fontSize = reqfs
  }

  /* lots of magic numbers here: the formula is:
   * lines_to_display - ( pixels_available / pixels_per_line )
   */
  const of = Math.max(Math.ceil(22 - (window.innerHeight / (minfont * 1.45))), 0)
  if (overflow !== of) {
    overflow = of
    doUpdate |= 13
  }

  /* decide on default view if needed */
  if ((keep === -1) || (!keep && isPlay())) {
    if (portrait) {
      switchView(1)
      switchTabByRef('dnpfav', 0)
    } else {
      switchView(0)
    }
  }

  const maintab = isActive('extra0')
  enableElement('uiextra', maintab)

  /* maintab scales to width too */
  if (maintab) {
    enableElement('pscroll', !smallUI)
    enableElement('nscroll', !smallUI)
    h = Math.min(window.innerWidth, h)
    if (smallUI) {
      lines = 7.5
    } else {
      lines = 10.75
    }
    if (isstream) {
      lines = lines - 2
    }
  } else {
    lines = 28
  }

  /* toolbars should grow/shrink with own magnification */
  /* try to have at leat 20 characters of the minimal font */
  bsize = Math.max(window.innerWidth, 20 * minfont)

  /* scale the upper font size boundary for the toolbars */
  if (h <= 200) {
    maxfont = minfont
  }
  if ((h > 200) && (h < 500)) {
    maxfont = minfont + ((h - 200) / (300 / (maxfont - minfont)))
  }

  bsize = Math.min(bsize / 20, maxfont)
  document.getElementById('playpack').style.fontSize = bsize + 'px'
  document.getElementById('viewtabs').style.fontSize = bsize + 'px'

  /* subtract button bars from the total height */
  h = h - (4 * bsize)

  fsize = h / lines
  /* font shall never get smaller than minfont */
  if (fsize < minfont) {
    fsize = minfont
  }

  document.body.style.fontSize = fsize + 'px'

  /* show clock during play */
  if (smallUI === 2) {
    power(0)
    return
  }

  /* nothing scrolls elsewhere */
  if (!maintab) {
    return
  }

  for (i = 0; i < numscrolls; i++) {
    var scroll = scrolls[i]
    var element = scroll.element
    element.style.left = 'auto'
    element.style.right = 'auto'
    var offRight = window.getComputedStyle(element).right
    var right = parseInt(offRight, 10)
    scroll.offset = offRight
    if (offRight.charAt(0) === '-') {
      element.style.right = offRight
      /* dummy call to force reset of element */
      right = element.offsetHeight
      element.style.transition = 'right 5s ease-in-out'
    } else {
      element.style.left = (right / 2) + 'px'
    }
  }
}

/*
 * sets up the scroller object
 */
function initScroll (index, id) {
  if (scrolls[index] === undefined) {
    var element = document.getElementById(id)
    var scroll = { id: id, element: element, offset: '0px' }
    scrolls[index] = scroll
    if (index + 1 > numscrolls) {
      numscrolls = index + 1
    }
  }
}

/*
 * start scrollers onload - see HTML
 */
function initScrolls () {
  initScroll(0, 'prev')
  initScroll(1, 'title')
  initScroll(2, 'next')
  initScroll(3, 'artist')
  initScroll(4, 'album')
}

/*
 * stop updates and make sure that error messages do not stack
 */
function fail (msg) {
  if (doUpdate >= 0) {
    toval = 5000
    doUpdate = -1
    setElement('prev', '')
    setElement('artist', '')
    setElement('title', 'Connection lost!')
    setElement('album', '..retrying..')
    setElement('next', '')
    adaptUI(-1)
  }
}

/*
 * toggle named tab, usually called by switchTab() but also used
 * to set active tab explicitly
 */
function switchTabByRef (element, num) {
  var i = 0
  var switched = 0
  var b = document.getElementById('c' + element + num)
  var e = document.getElementById(element + num)

  /* do not leave the available elements */
  if (e === null) {
    return 0
  }

  /* do not enable hidden tabs by accident */
  if (b.className === 'hide') {
    return 0
  }

  e = document.getElementById(element + i)
  while (e !== null) {
    b = document.getElementById('c' + element + i)
    if (b !== null) {
      if (i === parseInt(num)) {
        switched = 1
        b.className = 'active'
        e.className = 'active'
      } else if (b.className !== 'hide') {
        b.className = 'inactive'
        e.className = 'inactive'
      }
    } else {
      debugLog('No button c' + element + i + '!')
    }
    i++
    e = document.getElementById(element + i)
  }

  return switched
}

/**
 * toggle tabified result tabs
 */
function switchTab (ref) {
  var name = ref.getAttribute('data-name')
  var num = parseInt(ref.getAttribute('data-num'))
  switchTabByRef(name, num)
  adaptUI(1)
}

/*
 * toggle main UI tabs
 */
function switchView (element) {
  switchTabByRef('extra', element)
  adaptUI(1)
}

/*
 * add a line of text to the message pane. Acts as ringbuffer
 */
function addText (text) {
  var line = ''
  var numlines = 13
  var b = document.getElementById('cextra2')
  if ((text.charAt(0) !== '+') && (b.className === 'inactive')) {
    b.className = 'alert'
  }

  var e = document.getElementById('messages')

  if (msgpos < numlines) {
    msglines.push(text)
    msgpos++
  } else {
    for (var i = 0; i < numlines - 1; i++) {
      msglines[i] = msglines[i + 1]
    }
    msglines[numlines - 1] = text
  }

  for (i = 0; i < msgpos; i++) {
    if (msglines[i] !== '') {
      line += msglines[i] + '<br>\n'
    }
  }
  e.innerHTML = line
}

var moveline = ''
/*
 * send a command with optional argument to the server
 */
function sendCMDArg (cmd, arg) {
  var xmlhttp = new window.XMLHttpRequest()
  var code
  var e
  var text

  debugLog('send ' + cmd + ' - ' + arg)

  /* do not send commands if we know that we are offline */
  if (doUpdate < 0) {
    return
  }

  /* dirty trick to recognize a move... */
  if (cmd === 'move') {
    moveline = 'start'
    debugLog('Start move')
    return
  }

  if (cmd === 'download') {
    download(arg)
    return
  }

  if (cmd === 'shortcut') {
    arg = arg + ''
    for (var i = 0; i < shortcuts.length; i++) {
      if (arg === shortcuts[i]) return
    }
    shortcuts.push(arg)
    setCookies()
    doUpdate |= 8
    return
  }

  if (cmd === 'remsc') {
    arg = arg + ''
    var pos = shortcuts.indexOf(arg + '')
    if (~pos) {
      shortcuts.splice(pos, 1)
      setCookies()
      doUpdate |= 8
    } else {
      showConfirm(arg + ' is no shortcut in ' + shortcuts + '?!')
    }
    return
  }

  cmd = Number(cmd)
  code = Number(cmd).toString(16)

  /* replay, prev and next don't make sense on stream */
  if (isstream) {
    if ((cmd === 0x02) || (cmd === 0x03) || (cmd === 0x05)) {
      return
    }
    if ((cmd === 0x0) && (idletime > 0)) {
      setElement('title', '-- reconnect --')
    }
  }

  while (code.length < 4) {
    code = '0' + code
  }

  /* clear title results after add all */
  if ((arg === '0') &&
     ((cmd === 0x100c) || (cmd === 0x1014))) {
    e = document.getElementById('search0')
    text = document.createElement('em')
    text.innerHTML = '.. done ..'
    replaceChild(e, text)
  }

  switch (cmd & 0x00ff) {
    case 0x06: /* mpc_profile */
      adaptUI(-1)
      break
    case 0x18: /* mpc_remprof */
      showConfirm('Remove ' + arg + '?', 0x98)
      return
    case 0x98: /* confirm remprof */
      cmd = 0x18
      break
    /* all these commands have a progress */
    case 0x13: /* mpc_search */
      if (cmdtosend === '') {
        switchView(3)
        e = document.getElementById('search0')
        text = document.createElement('em')
        text.innerHTML = '.. searching ..'
        replaceChild(e, text)
        e = document.getElementById('search1')
        text = document.createElement('em')
        text.innerHTML = '.. searching ..'
        replaceChild(e, text)
        e = document.getElementById('search2')
        text = document.createElement('em')
        text.innerHTML = '.. searching ..'
        replaceChild(e, text)
      }
    /* fallthrough */
    case 0x08: /* mpc_dbclean */
    case 0x0b: /* mpc_doublets */
    case 0x12: /* mpc_dbinfo */
      if (cmdtosend === '') {
        cmdtosend = cmd
        argtosend = arg
      } else {
        showConfirm('Busy, sorry.')
      }
      return
  }

  xmlhttp.onreadystatechange = function () {
    if (xmlhttp.readyState === 4) {
      switch (xmlhttp.status) {
        case 0:
          fail('CMD Error: connection lost!')
          break
        case 200:
          showConfirm('One shot command returned unexpected data!<br>' + xmlhttp.responseText)
          /* fallthrough */
        case 204:
          /* TODO: this is not correct! */
          if (doUpdate < 0) {
            document.location.reload()
          }
          break
        case 503:
          showConfirm('Sorry, we\'re busy!')
          break
        default:
          fail('Received Error ' + xmlhttp.status + ' after sending 0x' + code)
      }
    }
  }

  /* one shots - fire and forget */
  var json = JSON.stringify({ cmd: cmd, arg: arg, clientid: 0 })
  debugLog('oreq: ' + json)

  xmlhttp.open('POST', '/mpctrl/cmd?' + json, true)
  xmlhttp.send()
}

/* send command without arguments */
function sendCMD (cmd) {
  sendCMDArg(cmd, '')
}

/*
 * use scrollwheel to control volume
 */
function volWheel (e) {
  e.preventDefault()
  if (e.deltaY < 0) {
    sendCMD(0x0d)
  } else if (e.deltaY > 0) {
    sendCMD(0x0e)
  }
}

function progWheel (e) {
  e.preventDefault()
  if (e.deltaY < 0) {
    sendCMD(0x0f)
  } else if (e.deltaY > 0) {
    sendCMD(0x10)
  }
}

/*
 * tap on the volume bar.
 * On mute just unmute, otherwise:
 * left third  - decrease volume
 * mid third   - mute
 * right third - quieter
 */
function ctrlVol (e) {
  const pos = e.clientX - this.offsetLeft
  const third = this.clientWidth / 3
  if (curvol === -2) {
    sendCMD(0x1d)
  } else {
    if (pos < third) {
      sendCMD(0x0e)
    } else if (pos > (2 * third)) {
      sendCMD(0x0d)
    } else {
      sendCMD(0x1d)
    }
  }
}

function ctrlFF (e) {
  const pos = e.clientX - this.offsetLeft
  const half = this.clientWidth / 2
  if (pos < half) {
    sendCMD(0x10)
  } else {
    sendCMD(0x0f)
  }
}

/*
 * change text in an element
 */
function setElement (e, val) {
  document.getElementById(e).innerHTML = val
}

/*
 * show/hide an element
 * if an element is defined with class='hide' and the display class should not
 * be empty, then data-class must be set to the desired class name!
 */
function enableElement (e, i) {
  const el = document.getElementById(e)
  if (el !== null) {
    if (i) {
      if (el.className === 'hide') {
        el.className = el.getAttribute('data-class')
      }
    } else {
      if (el.className !== 'hide') {
        el.setAttribute('data-class', el.className)
        el.className = 'hide'
      }
    }
  } else {
    debugLog('Element ' + e + ' does not exist!')
  }
}

/**
 * wrapper to create a popup with a FAV/DNP line
 */
function getPattern (choice, cmd, line, lineid) {
  var text = ''
  switch (line.charAt(0)) {
    case 't':
    case 'd':
      text = 'Title '
      break
    case 'a':
      text = 'Artist '
      break
    case 'l':
      text = 'Album '
      break
    case 'g':
      text = 'Genre '
      break
    case 'p':
      text = 'Path '
      break
    default:
      text = line.charAt(0) + '? '
      break
  }
  switch (line.charAt(1)) {
    case '*':
      text += '~ '
      break
    case '=':
      text += '= '
      break
    default:
      text += line.charAt(1) + '? '
      break
  }
  text += line.substring(2)
  return popselect([[choice, cmd]], line, text, 0, lineid)
}

/* creates a selection in a popselect popup */
function clickable (text, cmd, arg, id) {
  var reply = document.createElement('em')
  reply.className = 'clickline'
  reply.setAttribute('data-arg', arg)
  reply.setAttribute('data-cmd', cmd)
  reply.onclick = function () {
    const popup = document.getElementById('popup' + id)
    if (popup) {
      const dcmd = this.getAttribute('data-cmd')
      sendCMDArg(dcmd, this.getAttribute('data-arg'))
      // Hide the line on mpcmds
      if (dcmd > 0) {
        const line = document.getElementById('line' + id)
        /* line may be gone as sendcmd() already cleaned up search view */
        if (line) {
          line.className = 'hide'
          wipeElements(line)
        }
      }
      currentPop = ''
    }
  }
  reply.innerHTML = '&nbsp;' + text
  return reply
}

function setParentFontSize (ident, size) {
  const e = document.getElementById('line' + ident)
  if (e) {
    e.style.fontSize = size
  }
}

function togglePopup (ident) {
  if (moveline !== '') {
    const line = document.getElementById('line' + ident)
    if (!line) {
      document.log('line' + ident + 'is gone')
      doUpdate |= 1
      return
    }
    const tnum = line.getAttribute('data-tnum')
    if (moveline === 'start') {
      // initial drop on itself starts the actual move and closes the popup
      moveline = tnum
      line.style.color = 'red'
    } else if ((!tnum) || (moveline === tnum)) {
      // illegal Target, trigger refresh
      doUpdate |= 1
      moveline = ''
      return
    } else {
      // good target initiate move
      line.style.color = 'green'
      sendCMDArg(0x11, moveline + '/' + tnum)
      moveline = ''
      return
    }
  }

  const popup = document.getElementById('popup' + ident)
  if (!popup) {
    debugLog('Unknown popup "popup' + ident + '"!')
    return
  }

  if (!popup.classList.toggle('show')) {
    // Just closed the popup
    setParentFontSize(ident, '')
    currentPop = ''
  } else {
    // Opened the popup
    setParentFontSize(ident, '1.5em')
    if ((currentPop !== '') && (currentPop !== ident)) {
      // Clean up other popup
      const oldpop = document.getElementById('popup' + currentPop)
      if (!oldpop) {
        debugLog('Unknown popup "popup' + currentPop + '"!')
      } else {
        oldpop.classList.remove('show')
        setParentFontSize(currentPop, '')
      }
    }
    currentPop = ident
  }
}

/* for hardcoded popups, only show when the player is not in stream mode */
function togglePopupDB (ident) {
  if (!isstream) {
    togglePopup(ident)
  } else {
    sendCMD(0x00)
  }
}

/* returns a <div> with text that when clicked presents the choices */
function popselect (choice, arg, text, drag, id) {
  var i
  var select
  var reply = document.createElement('p')
  if (drag & 1) {
    choice.push(['&#x2195;', 'move']) // move
  }
  const num = choice.length
  reply.innerHTML = text
  if (num > 0) {
    reply.className = 'popselect'
    reply.id = 'line' + id
    reply.onclick = function () { togglePopup(id) }
    var popspan = document.createElement('span')
    popspan.className = 'popup'
    popspan.id = 'popup' + id
    for (i = 0; i < num; i++) {
      if (i !== 0) {
        select = document.createElement('b')
        select.innerText = ' /'
        popspan.appendChild(select)
      }
      select = clickable(choice[i][0], choice[i][1], arg, id)
      popspan.appendChild(select)
    }
    select = document.createElement('b')
    select.innerHTML = '\u2000\u274E'
    popspan.appendChild(select)
    reply.appendChild(popspan)
  } else {
    reply.className = 'nopopselect'
  }

  /* playlist ordering does not make sense in streams */
  if (!isstream) {
    reply.setAttribute('data-tnum', arg)
    /* element is draggable */
    if (drag & 1) {
      reply.draggable = 'true'
      reply.ondragstart = function (e) {
        e.dataTransfer.setData('title', arg)
        e.dataTransfer.setData('element', reply.id)
        e.dataTransfer.dropEffect = 'move'
      }
    }
    /* element is drop target */
    if (drag & 2) {
      reply.ondrop = function (e) {
        const source = parseInt(e.dataTransfer.getData('title'))
        if (source !== arg) {
          sendCMDArg(0x11, source + '/' + arg)
          enableElement(e.dataTransfer.getData('element'), 0)
        }
      }
      reply.ondragover = function (e) {
        e.preventDefault()
      }
    }
  }
  return reply
}

/*
 * parent: parent container to put list in
 * name: unique name for tab control
 * list: array of DOM elements to tabify
 */
function tabify (parent, name, list, maxlines) {
  var num = list.length
  var tabs
  /* use five lines at the very least! */
  maxlines = Math.max((maxlines - overflow), 5)
  if (num > 0) {
    tabs = parseInt((num - 1) / maxlines)
  }

  if (tabs > 0) {
    for (var i = 0; i <= tabs; i++) {
      var tabswitch = document.createElement('button')
      tabswitch.id = 'c' + name + i
      if (i === 0) {
        tabswitch.className = 'active'
      } else {
        tabswitch.className = 'inactive'
      }
      tabswitch.setAttribute('data-num', i)
      tabswitch.setAttribute('data-name', name)
      tabswitch.onclick = function () { switchTab(this) }
      tabswitch.innerHTML = '[' + i + ']'
      parent.appendChild(tabswitch)
    }
    for (i = 0; i <= tabs; i++) {
      var tabdiv = document.createElement('div')
      tabdiv.id = name + i
      if (i === 0) {
        tabdiv.className = 'active'
      } else {
        tabdiv.className = 'inactive'
      }
      tabdiv.width = '100%'
      tabdiv.addEventListener('touchstart', touchstartEL, { passive: true })
      tabdiv.addEventListener('touchend', touchendEL, false)
      parent.appendChild(tabdiv)
      for (var j = 0; (j < maxlines) &&
          ((maxlines * i) + j < num); j++) {
        tabdiv.appendChild(list[(maxlines * i) + j])
      }
    }
  } else {
    for (i = 0; i < num; i++) {
      parent.appendChild(list[i])
    }
  }
}

/* add play- favplay- and skipcount when debug is enabled */
function countPrefix (title) {
  var prefix = ''
  if (debug && !isstream) {
    prefix += '['
    if (!favplay) {
      prefix += title.playcount + '/'
    }
    prefix += title.favpcount + '/'
    prefix += title.skipcount + ']'
    if (!(title.flags & 8)) {
      /* added through search */
      prefix += '*'
    } else if (title.flags & 1) {
      /* favourite */
      prefix += '+'
    } else {
      prefix += '-'
    }
  }
  return prefix
}

function fullUpdate (data) {
  var choices = []
  var e = document.getElementById('dnpfav0')
  var maxnext = 10
  var maxprev = 4
  maxprev = Math.max((maxprev - overflow), 0)
  if (isstream) {
    maxnext = 15
  }
  var lineid = 1000

  /* display at least one next title */
  wipeElements(e)
  document.title = data.current.artist + ' - ' + data.current.title

  /* previous titles */
  if (data.prev.length > 0) {
    if (isstream) {
    /* only the stream title */
      setElement('prev', data.prev[0].title)
    } else {
      setElement('prev', data.prev[0].artist + ' - ' + data.prev[0].title)
    }
    for (var i = Math.min(maxprev, data.prev.length - 1); i >= 0; i--) {
      var titleline = countPrefix(data.prev[i])
      var cline
      if (data.prev[i].artist.length > 0) {
        titleline += data.prev[i].artist + ' - '
      }
      titleline += data.prev[i].title
      if (isstream) {
        cline = document.createElement('p')
        cline.className = 'nopopselect'
        cline.innerHTML = data.prev[i].title
      } else {
        choices = []
        choices.push(['&#x2620;', 0x100a])
        if (!(data.prev[i].flags & 1)) {
          choices.push(['&#x2665;', 0x1009])
        }
        choices.push(['&#x25B6;', 0x0011]) // re-play
        choices.push(['&#x1f4be;', 'download'])
        cline = popselect(choices,
          data.prev[i].key, titleline, 1, lineid++)
      }
      e.appendChild(cline)
    }
  } else {
    setElement('prev', '')
  }

  /* current title */
  setElement('title', data.current.title)
  setElement('artist', data.current.artist)
  setElement('album', data.current.album)

  /* update current title in the playlist */
  titleline = countPrefix(data.current)
  if (data.current.artist.length > 0) {
    titleline += data.current.artist + ' - '
  }
  titleline += data.current.title
  if (isstream) {
    cline = document.createElement('p')
    cline.id = 'ctitle'
    cline.className = 'nopopselect'
    cline.innerHTML = '&#x25B6; ' + titleline
    cline.onclick = function () { sendCMD(0x00) }
  } else {
    choices = []
    choices.push(['&#x2620;', 0x100a]) /* DNP */
    if (!(data.current.flags & 1)) {
      choices.push(['&#x2665;', 0x1009]) /* FAV */
    }
    choices.push(['&#x1f4be;', 'download']) // download
    cline = popselect(choices,
      data.current.key, '&#x25B6; ' + titleline, 0, lineid++)
    cline.className = 'ctitle'
  }
  e.appendChild(cline)

  /* next titles */
  if (data.next.length > 0) {
    if (data.next[0].artist === '') {
      /* only the stream title */
      setElement('next', data.next[0].title)
    } else {
      setElement('next', data.next[0].artist + ' - ' + data.next[0].title)
    }
    for (i = 0; i < Math.min(data.next.length, maxnext); i++) {
      titleline = countPrefix(data.next[i])
      if (data.next[i].artist.length > 0) {
        titleline += data.next[i].artist + ' - '
      }
      titleline += data.next[i].title
      if (isstream) {
        cline = document.createElement('p')
        cline.className = 'nopopselect'
        cline.innerHTML = titleline
      } else {
        choices = []
        choices.push(['&#x2620;', 0x100a])
        if (!(data.next[i].flags & 1)) {
          choices.push(['&#x2665;', 0x1009])
        }
        choices.push(['X', 0x001c])
        choices.push(['&#x1f4be;', 'download']) // download
        cline = popselect(choices,
          data.next[i].key, titleline, 3, lineid++)
      }
      e.appendChild(cline)
    }
  } else {
    setElement('next', '')
  }

  if (!isstream) {
    enableElement('fav', !(data.current.flags & 1))
    enableElement('dnp', !(data.current.flags & 2))
  }

  adaptUI(1)
}

function wipeElements (e) {
  while (e.hasChildNodes()) {
    wipeElements(e.firstChild)
    e.removeChild(e.firstChild)
  }
}

function searchUpdate (data) {
  var items = []
  var choices = []
  var i
  var lineid = 2000

  /* albums */
  var e = document.getElementById('search2')
  wipeElements(e)
  if (data.albums.length > 0) {
    enableElement('csearch2', 1)
    switchTabByRef('search', 2)
    for (i = 0; i < data.albums.length; i++) {
      choices = []
      choices.push(['&#x26b2;', 0x0413]) // search album
      if (!favplay || data.searchDNP) {
        choices.push(['&#x2665;', 0x0409]) // fav
      }
      if (!data.searchDNP) {
        choices.push(['&#x2620;', 0x040a]) // dnp
      }

      items[i] = popselect(choices,
        data.albums[i],
        data.albart[i] + ' - ' + data.albums[i], 0, lineid++)
    }
  } else {
    enableElement('csearch2', 0)
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No albums found!'
  }
  tabify(e, 'lres', items, 13)

  /* artists */
  e = document.getElementById('search1')
  wipeElements(e)
  items = []
  if (data.artists.length > 0) {
    enableElement('csearch1', 1)
    switchTabByRef('search', 1)
    for (i = 0; i < data.artists.length; i++) {
      choices = []
      choices.push(['&#x26b2;', 0x0213]) // search
      if (!favplay || data.searchDNP) {
        choices.push(['&#x2665;', 0x0209]) // fav
      }
      if (!data.searchDNP) {
        choices.push(['&#x2620;', 0x020a]) // dnp
      }
      items[i] = popselect(choices,
        data.artists[i],
        data.artists[i], 0, lineid++)
    }
  } else {
    enableElement('csearch1', 0)
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No artists found!'
  }
  tabify(e, 'ares', items, 13)

  /* titles */
  e = document.getElementById('search0')
  items = []
  wipeElements(e)
  if (data.titles.length > 0) {
    enableElement('csearch0', 1)
    switchTabByRef('search', 0)
    choices = []
    if (!favplay || data.searchDNP) {
      choices.push(['&#x2665;', 0x1009]) // fav
    }
    if (!data.searchDNP) {
      choices.push(['&#x2620;', 0x100a]) // dnp
    }
    choices.push(['&#x276f;', 0x100c]) // next
    choices.push(['&#x276f;&#x276f;', 0x1014]) // append
    items[0] = popselect(choices, 0, 'All results', 0, lineid++)

    for (i = 0; i < data.titles.length; i++) {
      choices = []
      if (!(data.titles[i].flags & 1)) {
        choices.push(['&#x2665;', 0x1009]) // fav
      }
      if (!(data.titles[i].flags & 2)) {
        choices.push(['&#x2620;', 0x100a]) // dnp
      }
      choices.push(['&#x276f;', 0x100c]) // next
      choices.push(['&#x276f;&#x276f;', 0x1014]) // append
      choices.push(['&#x1f4be;', 'download'])
      items[i + 1] = popselect(choices,
        data.titles[i].key,
        data.titles[i].artist + ' - ' + data.titles[i].title, 0, lineid++)
    }
  } else {
    enableElement('csearch0', 0)
    items[0] = document.createElement('em')
    items[0].innerHTML = 'Found ' + data.artists.length + ' artists and ' + data.albums.length + ' albums'
  }
  tabify(e, 'tres', items, 13)
}

function dnpfavUpdate (data) {
  var e = document.getElementById('dnpfav1')
  wipeElements(e)
  var items = []
  var i
  var lineid = 3000
  if (data.dnplist.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No DNPs yet'
  } else {
    for (i = 0; i < data.dnplist.length; i++) {
      items[i] = getPattern('Remove', 0x001a, data.dnplist[i], lineid++)
    }
  }
  tabify(e, 'dlist', items, 15)

  e = document.getElementById('dnpfav2')
  wipeElements(e)
  items = []
  if (data.favlist.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No favourites yet'
  } else {
    for (i = 0; i < data.favlist.length; i++) {
      items[i] = getPattern('Remove', 0x001b, data.favlist[i], lineid++)
    }
  }
  tabify(e, 'flist', items, 15)

  e = document.getElementById('dnpfav3')
  wipeElements(e)
  items = []
  if (data.dbllist.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No doublets yet'
  } else {
    for (i = 0; i < data.dbllist.length; i++) {
      items[i] = document.createElement('p')
      items[i].className = 'nopopselect'
      items[i].innerHTML = data.dbllist[i]
    }
  }
  tabify(e, 'llist', items, 15)
}

function secsToTime (secs) {
  var time = ''
  const hrs = Math.round(secs / 3600)
  secs = secs % 3600
  const min = Math.round(secs / 60)
  secs = secs % 60

  if (hrs !== 0) {
    if (hrs < 10) {
      time = '0'
    }
    time = time + hrs + ':'
  }
  if (min < 10) {
    time = time + '0'
  }
  time = time + min + ':'
  if (secs < 10) {
    time = time + '0'
  }
  time = time + secs
  return time
}

function playerUpdate (data) {
  if (isstream !== (data.mpmode & 1)) {
    isstream = (data.mpmode & 1) /* PM_STREAM */
    enableElement('goprev', !isstream)
    enableElement('gonext', !isstream)
    enableElement('dnp', !isstream)
    enableElement('setfavplay', !isstream)
    enableElement('cextra3', !isstream)
    enableElement('lscroll', !isstream)
    enableElement('cdnpfav0', !isstream)
    enableElement('cdnpfav1', !isstream)
    enableElement('cdnpfav2', !isstream)
    enableElement('cdnpfav3', !isstream)
    enableElement('rescan', !isstream)
    enableElement('dbinfo', !isstream)
    enableElement('playcnt', !isstream)
    enableElement('doublet', !isstream)
    enableElement('fav', !isstream)
    enableElement('clprof', !isstream)
  }

  if ((data.clientid > 0) && (clientid !== data.clientid)) {
    if (debug) {
      addText('Client ID from ' + clientid + ' to ' + data.clientid)
    }
    clientid = data.clientid
  }

  if (data.searchDNP) {
    document.getElementById('searchmode').innerHTML = 'DNP'
  } else {
    document.getElementById('searchmode').innerHTML = 'Active'
  }

  favplay = data.mpfavplay
  if (favplay) {
    document.getElementById('setfavplay').innerHTML = 'Disable Favplay'
  } else {
    document.getElementById('setfavplay').innerHTML = 'Enable Favplay'
  }

  /* enableElement('searchmode', favplay) */

  /* switching between stream and normal play */
  if (!isstream) {
    setElement('playtime', secsToTime(data.playtime) + '/' +
        secsToTime(data.remtime))
    document.getElementById('progressbar').style.width = data.percent + '%'
  } else {
    setElement('playtime', secsToTime(data.playtime))
    document.getElementById('progressbar').style.width = '100%'
  }

  if (active !== data.active) {
    if ((active !== 0) && (!data.type & 8)) {
      doUpdate |= 8
    }
    active = data.active
  }

  if ((active === 0) && (isstream)) {
    enableElement('schan', 1)
  } else {
    enableElement('schan', 0)
  }

  /* player status */
  switch (data.status) {
    case 0x22: /* mpc_idle */
    case 0x20: /* mpc_pause */
      setBody('pause')
      if (idlesleep > 0) {
        if (idletime < idlesleep) {
          idletime += toval
        } else {
          power(0)
        }
      }
      document.getElementById('play').innerHTML = '&#x25B6;'
      break
    case 0: /* mpc_play */
      clearBody()
      document.getElementById('play').innerHTML = '||'
      power(1)
      break
    default: /* player is busy */
      setBody('busy')
      power(1)
  }

  curvol = data.volume
  if (curvol > 0) {
    document.getElementById('volumebar').style.width = curvol + '%'
    document.getElementById('volumebar').className = ''
  } else if (curvol === -1) {
    enableElement('volume', 0)
  } else {
    document.getElementById('volumebar').className = 'mute'
  }

  if (data.msg !== '') {
    if (data.msg.startsWith('ALERT:')) {
      showConfirm(data.msg.substring(6))
    } else if (data.msg.startsWith('ACT:')) {
      setElement('title', '..' + data.msg.substring(4) + '..')
      adaptUI(1)
    } else {
      addText(data.msg)
    }
  }

  setElement('status', 'cmd:' + data.mpcmd + ' - status:' + data.mpstatus)
}

/*
 * get current status from the server and update the UI elements with the data
 * handles different types of status replies
 */
function updateUI () {
  var xmlhttp = new window.XMLHttpRequest()
  var json

  xmlhttp.onreadystatechange = function () {
    var data
    if (xmlhttp.readyState === 4) {
      switch (xmlhttp.status) {
        case 0:
          fail('CMD Error: connection lost!')
          break
        case 200:
          data = JSON.parse(xmlhttp.responseText)
          if (data !== undefined) {
            /* elCheapo locking */
            if (inUpdate++ > 1) {
              /* TODO turn this into debugLog */
              addText('Active update!')
              doUpdate |= data.type
            } else {
              /* standard update */
              playerUpdate(data)
              /* full update */
              if (data.type & 1) {
                fullUpdate(data)
              }
              /* search results */
              if (data.type & 2) {
                searchUpdate(data)
              }
              /* dnp/fav lists */
              if (data.type & 4) {
                dnpfavUpdate(data)
              }
              /* config */
              if (data.type & 8) {
                updateConfig(data)
              }
            }
            inUpdate--
          } else {
            debugLog('JSON-less reply on status 200!')
          }
          /* fallthrough */
        case 204:
          if (doUpdate < 0) {
            document.location.reload()
          }
          break
        case 503:
          showConfirm('Sorry, we\'re busy!')
          break
        default:
          fail('Received Error ' + xmlhttp.status)
      }
      if (doUpdate < 0) {
        document.body.className = 'disconnect'
      }

      setTimeout(function () { updateUI() }, toval)
    } /* replyState === 4 */
  } /* readyState callback */

  if (cmdtosend !== '') {
    /* 'synchronous' command - reply has info */
    json = JSON.stringify({ cmd: cmdtosend, arg: argtosend, clientid: clientid })
    debugLog('areq: ' + json)
    xmlhttp.open('POST', '/mpctrl/cmd?' + json, true)
    cmdtosend = ''
    argtosend = ''
  } else {
    if (doUpdate >= 0) {
      json = JSON.stringify({ cmd: doUpdate, clientid: clientid })
      doUpdate = 0
      debugLog('ureq: ' + json)
    } else {
      /* reconnect and fetch new clientID */
      clientid = -1
      json = JSON.stringify({ cmd: 0, clientid: clientid })
      debugLog('rreq: ' + json)
    }
    xmlhttp.open('GET', '/mpctrl/status?' + json, true)
  }
  xmlhttp.send()
}

/*
 * update the local shortcuts from cookie-data
 * config data is needed to align shortcut IDs with actual profiles/channels
 */
function updateShortcuts (data) {
  const e = document.getElementById('shortcuts')
  var items = []
  var i
  var name
  var choices
  var lineid = 5000

  wipeElements(e)
  if (shortcuts.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No shortcuts'
  } else {
    for (i = 0; i < shortcuts.length; i++) {
      var id = shortcuts[i]
      if (id !== 0) {
        if (id < 0) {
          name = data.sname[-id - 1]
        } else {
          name = data.profile[id - 1]
        }
        choices = []
        if (id !== -(active + 1)) {
          choices.push(['&#x25B6;', 0x06])
        } else {
          name = '&#x25B6; ' + name
        }
        choices.push(['X', 'remsc'])
        items[i] = popselect(choices, id,
          name, 0, lineid++)
      } else {
        debugLog('Illegal channel 0 in shortcuts! (' + i + ')')
      }
    }
  }
  tabify(e, 'sclist', items, 11)
}

/*
 * update basic configuration valöues from the reply
 */
function updateConfig (data) {
  var e
  var items = []
  var choices = []
  var i
  var lineid = 4000
  var name
  /* set profile list */
  e = document.getElementById('profiles')
  wipeElements(e)
  if (data.profile.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No profiles?'
  } else {
    for (i = 0; i < data.profile.length; i++) {
      name = data.profile[i]
      choices = []
      if (i !== (active - 1)) {
        choices.push(['&#x25B6;', 0x06])
        if (i !== 0) {
          choices.push(['X', 0x18])
        }
      } else {
        name = '&#x25B6; ' + name
      }
      choices.push(['&#x2665;', 'shortcut']) /* FAV */
      items[i] = popselect(choices, i + 1,
        name, 0, lineid++)
    }
  }
  tabify(e, 'prolist', items, 11)

  e = document.getElementById('channels')
  items = []
  wipeElements(e)
  if (data.sname.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No channels'
  } else {
    for (i = 0; i < data.sname.length; i++) {
      name = data.sname[i]
      choices = []
      if (i !== -(active + 1)) {
        choices.push(['&#x25B6;', 0x06])
        choices.push(['X', 0x18])
      } else {
        name = '&#x25B6; ' + name
      }
      choices.push(['&#x2665;', 'shortcut']) /* FAV */
      items[i] = popselect(choices, -(i + 1),
        name, 0, lineid++)
    }
  }
  tabify(e, 'chanlist', items, 11)

  updateShortcuts(data)

  if (active > 0) {
    if (favplay) {
      setElement('active', 'Playing favplay ' +
          data.profile[active - 1])
    } else {
      setElement('active', 'Playing profile ' +
          data.profile[active - 1])
    }
  } else if (active < 0) {
    setElement('active', 'Tuned in to ' +
          data.sname[-active - 1])
  } else {
    setElement('active', 'No active profile/channel')
  }
  /*  idlesleep = data.sleepto * 1000 */

  /* client debug is off but the server is in full debug mode */
  if ((debug === false) && (data.debug > 1)) {
    toggleDebug()
  }
}

function loadURL () {
  var url = document.getElementById('kbdtext').value
  if (url === '') {
    return
  }
  /* prefix http:// if needed */
  if (!url.toLowerCase().startsWith('http')) {
    url = 'http://' + url
  }
  /* check link format, this may be too strict */
  if (!url.endsWith('/') &&
     (url.indexOf('?') === -1) &&
     (url.indexOf('&') === -1) &&
     (url.indexOf(' ') === -1) &&
     (url.indexOf(',') === -1) &&
     (url.indexOf('!') === -1) &&
     (url.indexOf('.') !== -1)) {
    sendCMDArg(0x17, url)
  } else {
    showConfirm('Invalid Address!')
  }
}

function newActive () {
  document.getElementById('kbdtext').value = document.getElementById('prev').innerHTML
  showKbd(0x16)
}

/*
 * download a title by key(0=current)
 */
function download (key) {
  window.location = '/mpctrl/title/' + key
  doUpdate |= 1
}

/*
 * cycle through UI modes:
 * 0 - prev title, current title, next title
 * 1 - current title
 * 2 - clock
 */
function switchUI () {
  smallUI = (smallUI + 1) % 3
  if (smallUI === 2) {
    smallUI = -1
  } else {
    adaptUI(1)
    setCookies()
  }
}

/*
 * block the space key default event, so the page won't scroll when pausing
 * or entering text - this happens on keyup, so it can't be done in handleKey()
 */
function blockSpace (event) {
  /* only do this if the main or playlist view is visible! */
  if (isPlay() && (event.key === ' ')) {
    event.preventDefault()
  }
}

function toggleDebug () {
  debug = !debug
  if (debug) {
    addText('ClientID: ' + clientid)
    setElement('debug', 'Standard')
  } else {
    setElement('debug', 'Extra')
    document.getElementById('kbdtext').value = ''
  }
  enableElement('debugdiv', debug)
  doUpdate |= 1
}

/*
 * handle keydown events
 */
function handleKey (event) {
  var prevent = 1
  /* only do this if the main or playlist view is visible! */
  if (!isPlay()) {
    return
  }

  switch (event.key) {
    case ' ':
      sendCMD(0x0)
      break
    case 'p':
      sendCMD(0x02)
      break
    case 'n':
      sendCMD(0x03)
      break
    case 'f':
      sendCMD(0x1009)
      break
    case 'd':
      sendCMD(0x100a)
      break
    case '-':
      sendCMD(0x1d)
      break
    case '.':
      sendCMD(0x0d)
      break
    case ',':
      sendCMD(0x0e)
      break
    case 'G':
      toggleDebug()
      break
    default:
      prevent = 0
      debugLog('Pressed: ' + event.key)
      return
  }
  /* stop propagation if the event was handled here */
  if (prevent) {
    event.preventDefault()
  }
}

/**
 * touchscreen control
 */
function touchstartEL (event) {
  var touch = event.changedTouches[0]
  swipest.x = touch.pageX
  swipest.y = touch.pageY
  swipest.event = event
}

function touchendEL (event) {
  var touch = event.changedTouches[0]
  var dirx = 1
  const wwidth = window.innerWidth / 3
  var distx = touch.pageX - swipest.x
  const disty = Math.abs(touch.pageY - swipest.y)

  if (distx < 0) {
    dirx = -1
    distx = -distx
  }

  /* prevent false positives on vertical dragevents */
  if ((distx > disty) && (distx > wwidth)) {
    const button = document.getElementById('c' + this.id)
    if (!button) {
      debugLog('listener ' + this.id + 'has no Tab!')
      return
    }
    const name = button.getAttribute('data-name')
    var num = Number(button.getAttribute('data-num'))

    if (dirx > 0) {
      num--
    } else {
      num++
    }
    if (switchTabByRef(name, num) === 1) {
      event.stopPropagation()
      if (name === 'extra') {
        adaptUI(1)
      }
    }
  } else if (Math.max(distx, disty) < 5) {
    if (event.target.fireEvent !== undefined) {
      event.target.fireEvent('onclick')
    }
  }
}

function addTouch (name, num) {
  var el = document.getElementById(name + num)
  if (!el) {
    debugLog('There is no element ' + name + num)
  }
  el.setAttribute('data-num', num)
  el.setAttribute('data-name', name)
  el.addEventListener('touchstart', touchstartEL, { passive: true })
  el.addEventListener('touchend', touchendEL, false)
  el = document.getElementById('c' + name + num)
  if (!el) {
    debugLog('Element ' + name + num + ' has no Tab!')
  }
  el.setAttribute('data-num', num)
  el.setAttribute('data-name', name)
}

function power (on) {
  const el = document.getElementById('black')
  if (on === 1) {
    idletime = 0
    if (smallUI !== 2) {
      el.className = 'hide'
    }
  } else {
    el.className = ''
  }
}

function tap () {
  const el = document.getElementById('black')
  if (smallUI === -1) {
    smallUI = 2
    setCookies()
    el.className = ''
    return
  }
  if (smallUI === 2) {
    switchUI()
  }
  power(1)
}

function clocktime () {
  const now = new Date()
  const min = now.getMinutes()
  const hrs = now.getHours()

  var line = hrs + ':'
  if (hrs < 10) line = '0' + line
  if (min < 10) line = line + '0'
  setElement('time', '&nbsp;' + line + min + '&nbsp;')
  setElement('idleclock', line + min)

  setElement('idledate', now.toLocaleDateString('de-DE', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' }))
  setTimeout(function () { clocktime() }, 5000)
}

function addVolWheel (id) {
  const e = document.getElementById(id)
  if (e) {
    e.addEventListener('wheel', volWheel, { passive: false })
  } else {
    debugLog('Element ' + id + ' does not exist!')
  }
}

function setCookies () {
  var d = new Date()
  d.setTime(d.getTime() + (10 * 356 * 24 * 60 * 60 * 1000))
  if (smallUI === 0) {
    document.cookie = 'MPsmallUI=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;'
  } else {
    document.cookie = 'MPsmallUI=' + smallUI + '; expires=' + d.toUTCString() + '; path=/;'
  }
  var scbase = ''
  if (shortcuts.length > 0) {
    scbase = 'MPshortcuts=' + shortcuts[0]
    for (var i = 1; i < shortcuts.length; i++) {
      scbase += ',' + shortcuts[i]
    }
    scbase += '; expires=' + d.toUTCString() + '; path=/'
    document.cookie = scbase
  } else {
    document.cookie = 'MPshortcuts=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;'
  }
}

/*
 * start the UI update thread loops
 */
function initializeUI () {
  var cookieValue = 'none'
  if (document.cookie) {
    if (document.cookie.indexOf('MPshortcuts') !== -1) {
      cookieValue = document.cookie.split('; ').find(row => row.startsWith('MPshortcuts=')).split('=')[1]
      shortcuts = cookieValue.split(',')
    }
    if (document.cookie.indexOf('MPsmallUI') !== -1) {
      cookieValue = document.cookie.split('; ').find(row => row.startsWith('MPsmallUI=')).split('=')[1]
      smallUI = Number(cookieValue)
    }
  }
  for (var i = 0; i < 4; i++) {
    if (i < 3) {
      addTouch('search', i)
    }
    addTouch('tools', i)
    addTouch('extra', i)
    addTouch('dnpfav', i)
  }
  addVolWheel('viewtabs')
  addVolWheel('extra0')
  document.getElementById('progress').addEventListener('wheel', progWheel, { passive: false })
  document.getElementById('volume').addEventListener('click', ctrlVol, false)
  document.getElementById('progress').addEventListener('click', ctrlFF, false)
  document.body.addEventListener('keypress', handleKey)
  document.body.addEventListener('keyup', blockSpace)
  document.body.addEventListener('click', function () { tap() })
  /* set initial tab and sizes */
  adaptUI(0)
  /* initialize scrolltext */
  initScrolls()
  /* start update communication with server */
  updateUI()
  /* start scrolltext */
  scrollToggle()
  /* start clock */
  clocktime()
  /* attach virtual keyboard */
  initKbdDiv()
}
