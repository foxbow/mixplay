/* MPCOMM_STAT == 0 - normal update
   MPCOMM_FULLSTAT == 1 - +titles
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
var activecmd = -1
var smallUI = (document.cookie && (document.cookie.indexOf('MPsmallUI') !== -1))
var active = 0
var swipest = []
var overflow = 0

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
 * check if a long text needs to be scrolled and then scroll into the
 * proper direction
 */
function scrollToggle () {
  var to = 1000
  if (doUpdate === -1) return
  for (var i = 0; i < numscrolls; i++) {
    var element = scrolls[i].element
    if (scrolls[i].offset.charAt(0) === '-') {
      if (element.style.right === scrolls[i].offset) {
        element.style.right = '0px'
        to = 10000
      } else {
        element.style.right = scrolls[i].offset
        to = 15000
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
 */
function adaptUI (keep = 0) {
  /* Number of lines in sub-tabs */
  var lines
  var minfont = 13
  var h = window.innerHeight
  var i
  var fsize
  var bsize
  var fac

  /* decide on default view if needed */
  if (!keep && isPlay()) {
    if (window.innerWidth < h) {
      switchView(1)
      switchTabByRef('dnpfav', 0)
    } else {
      switchView(0)
    }
    return
  }

  const maintab = isActive('extra0')
  enableElement('uiextra', maintab)
  /* maintab scales to width too */
  if (maintab) {
    enableElement('pscroll', !smallUI)
    enableElement('nscroll', !smallUI)
    h = Math.min(window.innerWidth, h)
    if (smallUI) {
      lines = 11
    } else {
      lines = 14.5
    }
    if (isstream) {
      lines = lines - 2
    }
    minfont = 12
  } else if (isActive('extra1')) {
    lines = 31
  } else if (isActive('extra2')) {
    lines = 27
  } else if (isActive('extra3')) {
    lines = 31.5
  }

  /* font shall never get smaller than minfont */
  fsize = h / lines
  if (fsize < minfont) {
    fsize = minfont
    const of = Math.round(lines - (h / fsize) - 1)
    if (overflow !== of) {
      overflow = of
      doUpdate = 13
    }
  } else {
    overflow = 0
  }
  /* toolbars should grow/shrink with less magnification */
  bsize = minfont + ((2 * (fsize - minfont)) / 3)
  document.getElementById('playpack').style.fontSize = bsize + 'px'
  document.getElementById('viewtabs').style.fontSize = bsize + 'px'

  /* factor to fill gap between buttonbars */
  var screw = 4.9 /* number of 'lines' the buttonbars take */
  fac = (lines * fsize) / (((lines - screw) * fsize) + screw * bsize)
  document.body.style.fontSize = (fsize * fac) + 'px'

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
  if (doUpdate !== -1) {
    doUpdate = -1
    if (window.confirm(msg + '\nRetry?')) {
      document.location.reload()
    }
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
      console.log('No button c' + element + i + '!')
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
function switchView (element, keep = 1) {
  switchTabByRef('extra', element)
  adaptUI(keep)
}

/*
 * stop the server
 */
function pwSendCMD (msg, cmd) {
  var reply = window.prompt(msg)
  if ((reply !== null) && (reply !== '')) {
    sendCMD(cmd, reply)
  }
}

/*
 * add a line of text to the message pane. Acts as ringbuffer
 */
function addText (text) {
  var line = ''
  var numlines = 10
  var b = document.getElementById('cextra2')
  if (b.className === 'inactive') {
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

/*
 * send a command with optional argument to the server
 */
function sendCMD (cmd, arg = '') {
  var xmlhttp = new window.XMLHttpRequest()
  var code = Number(cmd).toString(16)
  var e
  var text
  cmd = Number(cmd)
  code = Number(cmd).toString(16)

  /* replay, prev and next don't make sense on stream */
  if (isstream && ((cmd === 0x02) || (cmd === 0x03) || (cmd === 0x05))) {
    return
  }

  /* ignore volume controls */
  if ((cmd !== 0x0d) && (cmd !== 0x0e) && (cmd !== 0x1d)) {
    /* avoid stacking  */
    if (activecmd !== -1) {
      return
    }
    activecmd = Number(cmd)
    /* give visual feedback that the command is being progressed */
    document.body.className = 'busy'
  }

  while (code.length < 4) {
    code = '0' + code
  }

  /* clear title results after add all */
  if ((arg === '0') &&
     ((code === '080c') || (code === '0814'))) {
    e = document.getElementById('search0')
    text = document.createElement('em')
    text.innerHTML = '.. done ..'
    replaceChild(e, text)
  }

  /* all these commands have a progress */
  switch (cmd & 0x00ff) {
    case 0x13: /* mpc_search */
      if (cmdtosend === '') {
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

    case 0x08: /* mpc_dbclean */
    case 0x0b: /* mpc_doublets */
    case 0x12: /* mpc_dbinfo */
      if (cmdtosend === '') {
        cmdtosend = code
        argtosend = arg
      } else {
        window.alert('Busy, sorry.')
      }
      activecmd = -1
      document.body.className = ''
      return
  }

  xmlhttp.onreadystatechange = function () {
    if (xmlhttp.readyState === 4) {
      switch (xmlhttp.status) {
        case 0:
          fail('CMD Error: connection lost!')
          break
        case 204:
          switch (activecmd) {
            case 0x0d: /* vol+ */
            case 0x0e: /* vol- */
            case 0x1d: /* mute */
              break
            default:
              activecmd = -1
          }
          break
        case 503:
          window.alert('Sorry, we\'re busy!')
          activecmd = -1
          break
        default:
          fail('Received Error ' + xmlhttp.status + ' after sending 0x' + code)
      }
      if (activecmd === -1) {
        document.body.className = ''
      }
    }
  }

  if (arg !== '') {
    xmlhttp.open('GET', '/mpctrl/cmd/' + code + '?' + arg, false)
  } else {
    xmlhttp.open('GET', '/mpctrl/cmd/' + code, false)
  }
  xmlhttp.send()
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
    console.log('Element ' + e + ' does not exist!')
  }
}

/**
 * wrapper to create a popup with a FAV/DNP line
 */
function getPattern (choice, cmd, line) {
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
  return popselect([[choice, cmd]], line, text, 0)
}

/* creates a selection in a popselect popup */
function clickable (text, cmd, arg, ident) {
  var reply = document.createElement('em')
  reply.className = 'clickline'
  reply.setAttribute('data-arg', arg)
  reply.setAttribute('data-cmd', cmd)
  reply.onclick = function () {
    const popup = document.getElementById('popup' + ident)
    if (popup) {
      const dcmd = this.getAttribute('data-cmd')
      if (dcmd !== -1) {
        sendCMD(dcmd, this.getAttribute('data-arg'))
        const line = document.getElementById('line' + ident)
        /* line may be gone as sendcmd() already cleaned up search view */
        if (line) {
          line.className = 'hide'
          wipeElements(line)
        }
      }
    }
  }
  reply.innerHTML = text
  return reply
}

function togglePopup (ident) {
  var popup = document.getElementById('popup' + ident)
  if (popup !== null) {
    popup.classList.toggle('show')
  }
}

/* returns a <div> with text that when clicked presents the choices */
function popselect (choice, arg, text, drag = 0) {
  const num = choice.length
  var i
  var select
  var reply = document.createElement('p')
  reply.innerText = text
  if (num > 0) {
    reply.className = 'popselect'
    const ident = choice[0][1] + '' + arg
    reply.id = 'line' + ident
    reply.onclick = function () { togglePopup(ident) }
    var popspan = document.createElement('span')
    popspan.className = 'popup'
    if (document.getElementById('popup' + ident)) {
      console.log('popup' + ident + ' already exists!')
    } else {
      popspan.id = 'popup' + ident
      for (i = 0; i < num; i++) {
        if (i !== 0) {
          select = document.createElement('b')
          select.innerText = '\u2000/\u2000 '
          popspan.appendChild(select)
        }
        select = clickable(choice[i][0], choice[i][1], arg, ident)
        popspan.appendChild(select)
      }
      select = document.createElement('b')
      select.innerText = '\u2000\u274E'
      popspan.appendChild(select)
      reply.appendChild(popspan)
    }
  } else {
    reply.className = 'nopopselect'
  }

  /* playlist ordering does not make sense in streams */
  if (!isstream) {
    /* element is draggable */
    if (drag & 1) {
      reply.draggable = 'true'
      reply.ondragstart = function (e) {
        e.dataTransfer.setData('title', arg)
        e.dataTransfer.setData('element', reply.id)
        e.dataTransfer.dropEffect = 'move'
      }
    }
    /* elemant is drop target */
    if (drag & 2) {
      reply.ondrop = function (e) {
        const source = parseInt(e.dataTransfer.getData('title'))
        if (source !== arg) {
          sendCMD(0x11, source + '/' + arg)
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
function tabify (parent, name, list, maxlines = 14) {
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

function fullUpdate (data) {
  var choices = []
  var e = document.getElementById('dnpfav0')
  var maxnext = 10
  var maxprev = 4
  maxprev = Math.max((maxprev - overflow), 0)
  if (isstream) {
    maxnext = 15
  }
  /* display at least one next title */
  wipeElements(e)
  document.title = data.current.artist + ' - ' + data.current.title
  if (data.prev.length > 0) {
    if (isstream) {
    /* only the stream title */
      setElement('prev', data.prev[0].title)
    } else {
      setElement('prev', data.prev[0].artist + ' - ' + data.prev[0].title)
    }
    for (var i = Math.min(maxprev, data.prev.length - 1); i >= 0; i--) {
      var titleline = ''
      var cline
      if (!isstream && (data.prev[i].playcount >= 0)) {
        titleline += '[' + data.prev[i].playcount + '/' + data.prev[i].skipcount + '] '
      }
      if (data.prev[i].artist.length > 0) {
        titleline += data.prev[i].artist + ' - '
      }
      titleline += data.prev[i].title
      if (isstream) {
        cline = document.createElement('p')
        cline.className = 'popselect'
        cline.innerHTML = data.prev[i].title
      } else {
        choices = []
        choices.push(['DNP', 0x080a])
        if (!(data.prev[i].flags & 1)) {
          choices.push(['FAV', 0x0809])
        }
        choices.push(['Replay', 0x0011])
        cline = popselect(choices,
          data.prev[i].key, titleline, 1)
      }
      e.appendChild(cline)
    }
  } else {
    setElement('prev', '')
  }
  setElement('title', data.current.title)
  setElement('artist', data.current.artist)
  setElement('album', data.current.album)
  titleline = ''
  if (!isstream && (data.current.playcount >= 0)) {
    titleline += '[' + data.current.playcount + '/' + data.current.skipcount + '] '
  }
  if (data.current.artist.length > 0) {
    titleline += data.current.artist + ' - '
  }
  titleline += data.current.title
  cline = document.createElement('p')
  cline.id = 'ctitle'
  cline.innerHTML = '&#x25B6; ' + titleline
  cline.onclick = function () { sendCMD(0x00) }
  if (!isstream) {
    cline.ondrop = function (ev) {
      sendCMD(0x11, ev.dataTransfer.getData('title'))
      enableElement(ev.dataTransfer.getData('element'), 0)
    }
    cline.ondragover = function (ev) {
      ev.preventDefault()
    }
  }
  e.appendChild(cline)
  if (data.next.length > 0) {
    if (data.next[0].artist === '') {
      /* only the stream title */
      setElement('next', data.next[0].title)
    } else {
      setElement('next', data.next[0].artist + ' - ' + data.next[0].title)
    }
    for (i = 0; i < Math.min(data.next.length, maxnext); i++) {
      titleline = ''
      if (!isstream && (data.next[i].playcount >= 0)) {
        titleline = '[' + data.next[i].playcount + '/' + data.next[i].skipcount + '] '
      }
      if (data.next[i].artist.length > 0) {
        titleline += data.next[i].artist + ' - '
      }
      titleline += data.next[i].title
      if (isstream) {
        cline = document.createElement('p')
        cline.className = 'popselect'
        cline.innerHTML = titleline
      } else {
        choices = []
        choices.push(['DNP', 0x080a])
        if (!(data.next[i].flags & 1)) {
          choices.push(['FAV', 0x0809])
        }
        choices.push(['Remove', 0x001c])
        cline = popselect(choices,
          data.next[i].key, titleline, 3)
      }
      e.appendChild(cline)
    }
  } else {
    setElement('next', '')
  }

  if (!isstream) {
    enableElement('fav', !(data.current.flags & 1))
    setElement('download', 'Download ' + document.title)
  } else {
    enableElement('fav', 0)
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
  switchTabByRef('search', 0)
  var e = document.getElementById('search2')
  wipeElements(e)
  var items = []
  var choices = []
  var i
  if (data.albums.length > 0) {
    for (i = 0; i < data.albums.length; i++) {
      choices = []
      choices.push(['Search', 0x0413])
      if ((!favplay) || (favplay && data.fpcurrent)) {
        choices.push(['FAV', 0x0409])
      }
      if ((!favplay) || (favplay && !data.fpcurrent)) {
        choices.push(['DNP', 0x040a])
      }

      items[i] = popselect(choices,
        data.albums[i],
        data.albart[i] + ' - ' + data.albums[i])
    }
  } else {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No albums found!'
  }
  tabify(e, 'lres', items, 14)

  e = document.getElementById('search1')
  wipeElements(e)
  items = []
  if (data.artists.length > 0) {
    for (i = 0; i < data.artists.length; i++) {
      choices = []
      choices.push(['Search', 0x0213])
      if ((!favplay) || (favplay && data.fpcurrent)) {
        choices.push(['FAV', 0x0209])
      }
      if ((!favplay) || (favplay && !data.fpcurrent)) {
        choices.push(['DNP', 0x020a])
      }
      items[i] = popselect(choices,
        data.artists[i],
        data.artists[i])
    }
  } else {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No artists found!'
  }
  tabify(e, 'ares', items, 14)

  e = document.getElementById('search0')
  items = []
  wipeElements(e)
  if (data.titles.length > 0) {
    choices = []
    if ((!favplay) || (favplay && data.fpcurrent)) {
      choices.push(['FAV', 0x0809])
    }
    if ((!favplay) || (favplay && !data.fpcurrent)) {
      choices.push(['DNP', 0x080a])
      choices.push(['Insert', 0x080c])
      choices.push(['Append', 0x0814])
    }

    items[0] = popselect(choices, 0, 'All results')

    for (i = 0; i < data.titles.length; i++) {
      choices = []
      if ((!favplay) || (favplay && data.fpcurrent)) {
        choices.push(['FAV', 0x0809])
      }
      if ((!favplay) || (favplay && !data.fpcurrent)) {
        choices.push(['DNP', 0x080a])
        choices.push(['Insert', 0x080c])
        choices.push(['Append', 0x0814])
      }
      items[i + 1] = popselect(choices,
        data.titles[i].key,
        data.titles[i].artist + ' - ' + data.titles[i].title)
    }
  } else {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'Found ' + data.artists.length + ' artists and ' + data.albums.length + ' albums'
  }
  tabify(e, 'tres', items, 14)
}

function dnpfavUpdate (data) {
  var e = document.getElementById('dnpfav1')
  wipeElements(e)
  var items = []
  var i
  if (data.dnplist.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No DNPs yet'
  } else {
    for (i = 0; i < data.dnplist.length; i++) {
      items[i] = getPattern('Remove', 0x001a, data.dnplist[i])
    }
  }
  tabify(e, 'dlist', items, 16)

  e = document.getElementById('dnpfav2')
  wipeElements(e)
  items = []
  if (data.favlist.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No favourites yet'
  } else {
    for (i = 0; i < data.favlist.length; i++) {
      items[i] = getPattern('Remove', 0x001b, data.favlist[i])
    }
  }
  tabify(e, 'flist', items, 16)
}

function playerUpdate (data) {
  if (isstream !== (data.mpmode === 1)) {
    isstream = (data.mpmode === 1) /* PM_STREAM */
    enableElement('goprev', !isstream)
    enableElement('gonext', !isstream)
    enableElement('playtime', !isstream)
    enableElement('dnp', !isstream)
    enableElement('setfavplay', !isstream)
    enableElement('cextra3', !isstream)
    enableElement('lscroll', !isstream)
    enableElement('cdnpfav0', !isstream)
    enableElement('cdnpfav1', !isstream)
    enableElement('cdnpfav2', !isstream)
    enableElement('download', !isstream)
    enableElement('rescan', !isstream)
    enableElement('dbinfo', !isstream)
  }

  if (data.mpmode & 4) {
    activecmd = -2
    document.body.className = 'busy'
  } else {
    if (activecmd === -2) {
      activecmd = -1
      document.body.className = ''
    }
  }

  favplay = data.mpfavplay
  if (favplay) {
    if (!data.fpcurrent) {
      document.getElementById('searchmode').innerHTML = 'Current titles'
    } else {
      document.getElementById('searchmode').innerHTML = 'Database'
    }
    document.getElementById('setfavplay').innerHTML = 'Disable Favplay'
  } else {
    document.getElementById('setfavplay').innerHTML = 'Enable Favplay'
  }

  enableElement('searchmode', favplay)

  /* switching between stream and normal play */
  if (!isstream) {
    setElement('playtime', data.playtime)
    setElement('remtime', data.remtime)
    document.getElementById('progressbar').style.width = data.percent + '%'
  } else {
    setElement('remtime', data.playtime)
    document.getElementById('progressbar').style.width = '100%'
  }

  if (active !== data.active) {
    active = data.active
    /* The server should automatically send a full update on profile change
    doUpdate = 13
    */
  }

  if ((active === 0) && (isstream)) {
    enableElement('schan', 1)
    enableElement('cprof', 0)
  } else {
    enableElement('schan', 0)
    enableElement('cprof', 1)
  }

  enableElement('current', !data.status)
  enableElement('ctitle', !data.status)

  if (data.status) {
    document.getElementById('play').innerHTML = '\u25B6'
  } else {
    document.getElementById('play').innerHTML = '\u23f8'
  }
  if (data.volume > 0) {
    document.getElementById('volumebar').style.width = data.volume + '%'
    document.getElementById('volumebar').className = ''
  } else if (data.volume === -1) {
    enableElement('volume', 0)
  } else {
    document.getElementById('volumebar').className = 'mute'
  }

  if (data.msg !== '') {
    if (data.msg.startsWith('ALERT:')) {
      if (data.msg.startsWith('ALERT:Done.')) {
        addText(data.msg.substring(6))
      } else {
        window.alert(data.msg.substring(6))
      }
    } else {
      addText(data.msg)
    }
  }
}

/*
 * get current status from the server and update the UI elements with the data
 * handles different types of status replies
 */
function updateUI () {
  var xmlhttp = new window.XMLHttpRequest()

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
            /* standard update */
            playerUpdate(data)
          } else {
            console.log('JSON-less reply!')
          }
          break
        case 204:
          break
        case 503:
          window.alert('Sorry, we\'re busy!')
          break
        default:
          fail('Received Error ' + xmlhttp.status)
      }

      if (doUpdate !== -1) {
        setTimeout(function () { updateUI() }, 750)
      } else {
        document.body.className = 'disconnect'
      }
    }
  }

  if (doUpdate === -1) {
    document.body.className = 'disconnect'
    return
  } else if (cmdtosend !== '') {
    /* snchronous command */
    if (argtosend !== '') {
      xmlhttp.open('GET', '/mpctrl/cmd/' + cmdtosend + '?' + argtosend, true)
    } else {
      xmlhttp.open('GET', '/mpctrl/cmd/' + cmdtosend, true)
    }
    cmdtosend = ''
    argtosend = ''
  } else if (doUpdate === 0) {
    xmlhttp.open('GET', '/mpctrl/status', true)
  } else {
    xmlhttp.open('GET', '/mpctrl/status?' + doUpdate, true)
    doUpdate = 0
  }
  xmlhttp.send()
}

function updateConfig (data) {
  var e
  var items = []
  var choices = []
  var i
  /* set profile list */
  e = document.getElementById('profiles')
  wipeElements(e)
  if (data.profile.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No profiles?'
  } else {
    for (i = 0; i < data.profile.length; i++) {
      choices = []
      if (i !== (active - 1)) {
        choices.push(['Play', 0x06])
      } else {
        if (i !== 0) {
          choices.push(['Remove', 0x18])
        }
      }
      items[i] = popselect(choices, i + 1,
        data.profile[i])
    }
  }
  tabify(e, 'prolist', items, 10)

  e = document.getElementById('channels')
  items = []
  wipeElements(e)
  if (data.sname.length === 0) {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No channels'
  } else {
    for (i = 0; i < data.sname.length; i++) {
      choices = []
      if (i !== -(active + 1)) {
        choices.push(['Play', 0x06])
        choices.push(['Remove', 0x18])
      }
      items[i] = popselect(choices, -(i + 1),
        data.sname[i])
    }
  }
  tabify(e, 'chanlist', items, 10)
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
}

/*
 * send command with argument set in the 'text' element(Search)
 */
function sendArg (cmd) {
  if (isstream) return
  var term = document.getElementById('text').value
  if (term.length > 1) {
    sendCMD(cmd, term)
  } else {
    window.alert('Need at least two letters!')
  }
}

function loadURL2 (url) {
  if (!url) {
    console.log('loadURL() returned an invalid value!')
    url = ''
  }
  const parts = url.split('/')

  /* check result from clipboard */
  if (url.toLowerCase().startsWith('http') && !url.endsWith('/')) {
    if (!window.confirm('Load ' + parts[parts.length - 1] + '?')) {
      url = window.prompt('Enter Address to load')
    }
  } else {
    url = window.prompt('Enter Address to load')
  }

  if (url) {
    if (url.toLowerCase().startsWith('http')) {
      sendCMD(0x17, url)
      return
    }
    window.alert('Invalid Address!')
  }
}

/*
 * try to peek the clipboard. If the clipboard can be read forward
 * the contents to the actual loadURL function. Otherwise just send
 * an empty string.
 */
function loadURL () {
  /* plain http on mobile causes readText() to never return
     this may hit us on desktop soon as well =/ */
  if ((document.location.protocol !== 'https:') &&
      (typeof window.orientation !== 'undefined')) {
    loadURL2('')
  } else if (typeof navigator.clipboard.readText !== 'function') {
    loadURL2('')
  } else {
    navigator.clipboard.readText().then(clipText => {
      loadURL2(clipText)
    }).catch(err => {
      console.log('readText: ' + err)
      loadURL2('')
    })
  }
}

function newActive () {
  var name = ''
  if (isstream && (active === 0)) {
    name = document.getElementById('prev').innerText
  }
  if ((name.length < 3) || (!window.confirm('Save channel as ' + name + '?'))) {
    if (isstream && (active === 0)) {
      name = window.prompt('Save channel as:')
    } else {
      name = window.prompt('Name for new profile:')
    }
    if (!name) {
      return
    }
    if (name.length < 3) {
      window.alert('Please give a longer name')
      return
    }
  }
  sendCMD(0x16, name)
}

/*
 * download a title by key(0=current)
 */
function download (key = 0) {
  if (window.confirm('Download ' + document.getElementById('title').innerHTML + ' ?')) {
    window.location = '/title/' + key
  }
}

/**
 * helper function to react on enter in a text
 * input without form
 */
function isEnter (event, cmd) {
  if (event.keyCode === 13) {
    sendArg(cmd)
    return false
  }
}

function switchUI () {
  smallUI = !smallUI
  if (smallUI) {
    var d = new Date()
    d.setTime(d.getTime() + (10 * 356 * 24 * 60 * 60 * 1000))
    document.cookie = 'MPsmallUI=1; expires=' + d.toUTCString() + '; path=/'
  } else {
    document.cookie = 'MPsmallUI=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;'
  }
  adaptUI(1)
}

function blockSpace (event) {
  /* only do this if the main or playlist view is visible! */
  if (isPlay() && (event.key === ' ')) {
    event.preventDefault()
  }
}

function handleKey (event) {
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
      sendCMD(0x0809)
      break
    case 'd':
      sendCMD(0x080a)
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
    case 'Q':
      pwSendCMD('Really stop the server?', 0x07)
      break
    case 'D':
      pwSendCMD('Mark doublets?', 0x0b)
      break
    default:
      console.log('Pressed: ' + event.key)
      return
  }
  event.preventDefault()
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
      console.log('listener ' + this.id + 'has no Tab!')
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
    event.target.fireEvent('onclick')
  }
}

function addTouch (name, num) {
  var el = document.getElementById(name + num)
  if (!el) {
    console.log('There is no element ' + name + num)
  }
  el.setAttribute('data-num', num)
  el.setAttribute('data-name', name)
  el.addEventListener('touchstart', touchstartEL, { passive: true })
  el.addEventListener('touchend', touchendEL, false)
  el = document.getElementById('c' + name + num)
  if (!el) {
    console.log('Element ' + name + num + ' has no Tab!')
  }
  el.setAttribute('data-num', num)
  el.setAttribute('data-name', name)
}

/*
 * start the UI update thread loops
 */
function initializeUI () {
  for (var i = 0; i < 4; i++) {
    if (i < 3) {
      addTouch('tools', i)
      addTouch('search', i)
      addTouch('dnpfav', i)
    }
    addTouch('extra', i)
  }
  document.body.addEventListener('wheel', volWheel, { passive: false })
  document.body.addEventListener('keypress', handleKey)
  document.body.addEventListener('keyup', blockSpace)
  /* set initial tab and sizes */
  adaptUI(0)
  /* initialize scrolltext */
  initScrolls()
  /* start update communication with server */
  updateUI()
  /* start scrolltext */
  scrollToggle()
}
