/* -1: configuration; 0: no update; 1: normal updates */
var doUpdate = -1
var data = null
var mpver = Number('~~MPCOMM_VER~~')
var serverver = '~~MIXPLAY_VER~~'
var isstream = 0
var msglines = ['', '', '', '', '', '', '', '', '', '', '', '', '', '', '']
var msgpos = 0
var active = 0
var scrolls = []
var numscrolls = 0
var favplay = 0
var cmdtosend = ''
var argtosend = ''
var activecmd = -1
var smallUI = getsmallUI()

function getsmallUI () {
  if (document.cookie) {
    if (document.cookie.indexOf('MPsmallUI') !== -1) {
      return true
    }
  }
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
 * pops up the current build ID
 */
function showInfo () {
  window.alert('mixplayd ' + serverver + '(' + mpver + ')')
}

/*
 * check if a long text needs to be scrolled and then scroll into the
 * proper direction
 */
function scrollToggle () {
  var to = 1000
  if (doUpdate === 0) return
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

/*
 * Scales the font to fit into current window, enables scrolling
 * on texts that are longer than the parent containerand centers
 * shorter texts
 */
function adaptUI () {
  /* Number of lines in sub-tabs */
  var lines = 32.5
  var h = window.innerHeight
  const maintab = document.getElementById('extra0').className === 'active'

  enableElement('uiextra', maintab)
  /* mantab scales to width too and has less lines to display */
  if (maintab) {
    if (smallUI) {
      h = Math.min((window.innerWidth * 5) / 8, h)
      lines = 11.25
    } else {
      h = Math.min((window.innerWidth * 4) / 5, h)
      lines = 15.25
    }
  }

  /* font shall never get snmaller than 12px */
  document.body.style.fontSize = Math.max(h / lines, 12) + 'px'

  if (!maintab) {
    return
  }

  for (var i = 0; i < numscrolls; i++) {
    var scroll = scrolls[i]
    var element = scroll.element
    element.style.left = 'auto'
    element.style.right = 'auto'
    var offRight = getComputedStyle(element).right
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
  if (doUpdate !== 0) {
    doUpdate = 0
    if (window.confirm(msg + '\nRetry?')) {
      location.reload()
    }
  }
}

/*
 * set profile to sanitized value
 */
function setProf () {
  var e = document.getElementById('profiles')
  var id = e.value
  if (id !== 0) {
    /* pull main to front */
    switchView(0)
    sendCMD(0x06, id)
  } else {
    e.value = active
  }
}

/*
 * toggle named tab, usually called by switchTab() but also used
 * to set active tab explicitly
 */
function switchTabByRef (element, num) {
  var i = 0
  var b
  var e = document.getElementById(element + i)

  while (e !== null) {
    b = document.getElementById('c' + element + i)
    if (b !== null) {
      if (i === parseInt(num)) {
        b.className = 'active'
        e.className = 'active'
      } else {
        b.className = 'inactive'
        e.className = 'inactive'
      }
    } else {
      console.log('No button c' + element + i + '!')
    }
    i++
    e = document.getElementById(element + i)
  }
}

/**
 * toggle tabified result tabs
 */
function switchTab (ref) {
  var name = ref.id.substring(1, ref.id.length - 1)
  var num = parseInt(ref.id.substring(ref.id.length - 1))
  switchTabByRef(name, num)
}

/*
 * toggle main UI tabs
 */
function switchView (element) {
  switchTabByRef('extra', element)
  adaptUI()
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
    msglines[msgpos] = text
    msgpos++
  } else {
    for (var i = 0; i < numlines - 1; i++) {
      msglines[i] = msglines[i + 1]
    }
    msglines[numlines - 1] = text
  }

  for (i = 0; i < numlines; i++) {
    if (msglines[i] !== '') {
      line += msglines[i] + '<br>\n'
    }
  }
  e.innerHTML = line
}

function wipeLog () {
  var i
  var e = document.getElementById('messages')
  for (i = 0; i < 15; i++) {
    msglines[i] = ''
  }
  msgpos = 0
  e.innerHTML = '<em>No messages.</em>'
}

/*
 * send a command with optional argument to the server
 */
function sendCMD (cmd, arg = '') {
  var xmlhttp = new XMLHttpRequest()
  var code = Number(cmd).toString(16)
  var e
  var text

  /* ignore volume */
  if ((cmd !== 0x0d) && (cmd !== 0x0e)) {
    /* avoid stacking  */
    if (activecmd !== -1) {
      return
    }
    activecmd = cmd
    /* give visual feedback that the command is being progressed */
    document.body.className = 'busy'
  }

  while (code.length < 4) {
    code = '0' + code
  }

  /* these commands should pull main to front */
  if (code === '001e') switchView(0)

  /* clear title results after add all */
  if ((arg === '0') &&
     ((code === '080c') || (code === '0814'))) {
    e = document.getElementById('search0')
    text = document.createElement('em')
    text.innerHTML = '.. done ..'
    replaceChild(e, text)
  }

  if ((cmd & 0x00ff) === 0x0013) {
    if (cmdtosend === '') {
      cmdtosend = code
      argtosend = arg
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
    } else {
      window.alert('Wait for previous search to be sent')
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
          activecmd = -1
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
    xmlhttp.open('GET', '/cmd/' + code + '?' + arg, true)
  } else {
    xmlhttp.open('GET', '/cmd/' + code, true)
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
  return xpopselect([choice], [cmd], line, text, 0)
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
        line.className = 'hide'
        wipeElements(line)
      }
    } else {
      console.log('popup' + ident + 'does no longer exist')
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

/* returns a <div> with text that when clicked presents the two choices */
function xpopselect (choice, cmd, arg, text, drag = 0) {
  const num = choice.length
  var i
  var select
  var reply = document.createElement('p')
  reply.innerText = text
  reply.className = 'popselect'
  const ident = cmd[0] + arg
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
      select = clickable(choice[i], cmd[i], arg, ident)
      popspan.appendChild(select)
    }
    select = document.createElement('b')
    select.innerText = '\u2000\u274E'
    popspan.appendChild(select)
    reply.appendChild(popspan)
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
      tabswitch.onclick = function () { switchTabByRef(name, this.getAttribute('data-num')) }
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
  var e = document.getElementById('dnpfav0')
  wipeElements(e)
  document.title = data.current.artist + ' - ' + data.current.title
  if (data.prev.length > 0) {
    if (isstream) {
    /* only the stream title */
      setElement('prev', data.prev[0].title)
    } else {
      setElement('prev', data.prev[0].artist + ' - ' + data.prev[0].title)
    }
    for (var i = Math.min(4, data.prev.length - 1); i >= 0; i--) {
      var titleline = ''
      if (data.prev[i].playcount >= 0) {
        titleline += '[' + data.prev[i].playcount + '/' + data.prev[i].skipcount + '] '
      }
      titleline += data.prev[i].artist + ' - ' + data.prev[i].title
      var cline = xpopselect(['DNP', 'FAV', 'Replay'],
        [0x080a, 0x0809, 0x0011],
        data.prev[i].key, titleline, 1)
      e.appendChild(cline)
    }
  } else {
    setElement('prev', '- - -')
  }
  setElement('title', data.current.title)
  setElement('artist', data.current.artist)
  setElement('album', data.current.album)
  titleline = ''
  if (data.current.playcount >= 0) {
    titleline += '[' + data.current.playcount + '/' + data.current.skipcount + '] '
  }
  cline = document.createElement('p')
  cline.id = 'ctitle'
  cline.innerHTML = '&#x25B6; ' + titleline + data.current.artist + ' - ' + data.current.title
  if (!isstream) {
    cline.onclick = function () {
      sendCMD(0x00)
    }
    cline.ondrop = function (e) {
      sendCMD(0x11, e.dataTransfer.getData('title'))
      enableElement(e.dataTransfer.getData('element'), 0)
    }
    cline.ondragover = function (e) {
      e.preventDefault()
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
    for (i = 0; i < data.next.length; i++) {
      titleline = ''
      if (data.next[i].playcount >= 0) {
        titleline = '[' + data.next[i].playcount + '/' + data.next[i].skipcount + '] '
      }
      titleline += data.next[i].artist + ' - ' + data.next[i].title
      if (isstream) {
        cline = document.createElement('p')
        cline.className = 'popselect'
        cline.innerHTML = titleline
      } else {
        cline = cline = xpopselect(['DNP', 'FAV', 'Remove'],
          [0x080a, 0x0809, 0x001c],
          data.next[i].key, titleline, 3)
      }
      e.appendChild(cline)
    }
  } else {
    setElement('next', '- - -')
  }

  enableElement('fav', !(data.current.flags & 1))
  adaptUI()
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
  var cmds = []
  var i
  if (data.albums.length > 0) {
    for (i = 0; i < data.albums.length; i++) {
      choices = ['Search']
      cmds = [0x0413]
      if ((!favplay) || (favplay && data.fpcurrent)) {
        choices.push('FAV')
        cmds.push(0x0409)
      }
      if ((!favplay) || (favplay && !data.fpcurrent)) {
        choices.push('DNP')
        cmds.push(0x040a)
      }
      items[i] = xpopselect(choices, cmds,
        data.albums[i],
        data.albart[i] + ' - ' + data.albums[i])
    }
  } else {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No albums found!'
  }
  tabify(e, 'lres', items)

  e = document.getElementById('search1')
  wipeElements(e)
  items = []
  if (data.artists.length > 0) {
    for (i = 0; i < data.artists.length; i++) {
      choices = ['Search']
      cmds = [0x0213]
      if ((!favplay) || (favplay && data.fpcurrent)) {
        choices.push('FAV')
        cmds.push(0x0209)
      }
      if ((!favplay) || (favplay && !data.fpcurrent)) {
        choices.push('DNP')
        cmds.push(0x020a)
      }
      items[i] = xpopselect(choices, cmds,
        data.artists[i],
        data.artists[i])
    }
  } else {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'No artists found!'
  }
  tabify(e, 'ares', items)

  e = document.getElementById('search0')
  items = []
  wipeElements(e)
  if (data.titles.length > 0) {
    if (data.fpcurrent) {
      items[0] = document.createElement('a')
      items[0].href = '/cmd/0114?0'
      items[0].innerHTML = '-- Fav all --'
    } else {
      items[0] = xpopselect(['Insert', 'Append'],
        [0x080c, 0x0814],
        0, '-- Play all --')
    }
    for (i = 0; i < data.titles.length; i++) {
      if (favplay && data.fpcurrent) {
        choices = ['FAV']
        cmds = [0x0809]
      } else {
        choices = ['Insert', 'Append']
        cmds = [0x080c, 0x0814]
      }
      items[i + 1] = xpopselect(choices, cmds,
        data.titles[i].key,
        data.titles[i].artist + ' - ' + data.titles[i].title)
    }
  } else {
    items[0] = document.createElement('em')
    items[0].innerHTML = 'Found ' + data.artists.length + ' artists and ' + data.albums.length + ' albums'
  }
  tabify(e, 'tres', items)
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
  favplay = data.mpfavplay
  if (favplay) {
    if (data.fpcurrent) {
      document.getElementById('searchmode').innerHTML = 'Current titles'
    } else {
      document.getElementById('searchmode').innerHTML = 'Database'
    }
    document.getElementById('setfavplay').innerHTML = 'Disable Favplay'
  } else {
    document.getElementById('setfavplay').innerHTML = 'Enable Favplay'
  }

  enableElement('searchmode', favplay)
  enableElement('goprev', !isstream)
  enableElement('gonext', !isstream)
  enableElement('progress', !isstream)
  enableElement('remtime', !isstream)
  enableElement('setfavplay', !isstream)
  enableElement('cextra1', !isstream)
  enableElement('lscroll', !isstream)

  /* switching between stream and normal play */
  setElement('playtime', data.playtime)
  if (!isstream) {
    setElement('remtime', data.remtime)
    document.getElementById('progressbar').style.width = data.percent + '%'
  }

  if (active !== data.active) {
    active = data.active
    setActive(active)
  }

  enableElement('current', !data.status)
  if (data.status) {
    document.getElementById('play').value = '\u25B6'
  } else {
    document.getElementById('play').value = '\u23f8'
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
      if (!data.msg.startsWith('ALERT:Done.')) {
        window.alert(data.msg.substring(6))
      }
    } else {
      addText(data.msg)
    }
  }
}

function parseReply (reply) {
  var data = JSON.parse(reply)
  if (data !== undefined) {
    if (data.version !== mpver) {
      fail('Version clash, expected ' + mpver + ' and got ' + data.version)
      return
    }
    isstream = (data.mpmode === 1) /* PM_STREAM */

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

    /* standard update */
    playerUpdate(data)
  }
}

/*
 * get current status from the server and update the UI elements with the data
 * handles different types of status replies
 */
function updateUI () {
  var xmlhttp = new XMLHttpRequest()
  xmlhttp.onreadystatechange = function () {
    if (xmlhttp.readyState === 4) {
      switch (xmlhttp.status) {
        case 0:
          fail('CMD Error: connection lost!')
          break
        case 200:
          parseReply(xmlhttp.responseText)
          break
        case 204:
          break
        case 503:
          window.alert('Sorry, we\'re busy!')
          break
        default:
          fail('Received Error ' + xmlhttp.status)
      }

      if (doUpdate !== 0) {
        setTimeout(function () { updateUI() }, 750)
      } else {
        document.body.className = 'disconnect'
      }
    }
  }

  if (doUpdate === -1) {
    setTimeout(function () { getConfig() }, 333)
    doUpdate = 1
  }

  if (cmdtosend !== '') {
    /* snchronous command */
    if (argtosend !== '') {
      xmlhttp.open('GET', '/cmd/' + cmdtosend + '?' + argtosend, true)
    } else {
      xmlhttp.open('GET', '/cmd/' + cmdtosend, true)
    }
    cmdtosend = ''
    argtosend = ''
  } else {
    /* normal status update */
    xmlhttp.open('GET', '/status', true)
  }
  xmlhttp.send()
}

/*
 * shows the current profile/channel in the profile select dropdown
 */
function setActive (id) {
  var s = document.getElementById('profiles')
  s.value = id
}

/*
 * gets the basic configuration of the server
 * this should only happen once at start
 */
function getConfig () {
  var xmlhttp = new XMLHttpRequest()
  xmlhttp.onreadystatechange = function () {
    if (xmlhttp.readyState === 4) {
      if (xmlhttp.status === 200) {
        data = JSON.parse(xmlhttp.responseText)
        if (data !== undefined) {
          if (data.version !== mpver) {
            fail('Version clash, expected ' + mpver + ' and got ' + data.version)
            return
          }
          if (data.type === -1) {
            var s = document.getElementById('profiles')
            s.options.length = 0
            for (var i = 0; i < data.config.profiles; i++) {
              s.options[s.options.length] = new Option(data.config.profile[i], i + 1)
            }
            s.options[s.options.length] = new Option('None', 0)
            for (i = 0; i < data.config.streams; i++) {
              s.options[s.options.length] = new Option(data.config.sname[i], -(i + 1))
            }
            active = 0
          } else {
            fail('Received reply of type ' + data.type + ' for config!')
          }
        } else {
          fail('Received no Data for config!')
        }
      } else {
        fail('Received Error ' + xmlhttp.status + ' trying to get config')
      }
    }
  }

  xmlhttp.open('GET', '/config', true)
  xmlhttp.send()
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

/*
 * creates a new profile or loads a channel
 */
function createLoad () {
  var term = document.getElementById('ptext').value
  var asking = ''
  if (term.length < 3) {
    window.alert('Need at least two letters!')
  } else {
    if (term.toLowerCase().startsWith('http')) {
      sendCMD(0x17, term)
    } else {
      if (isstream) {
        asking = 'Add current stream as channel ' + term + ' ?'
      } else {
        asking = 'Create new profile ' + term + ' ?'
      }
      if (window.confirm(asking)) {
        sendCMD(0x16, term)
        doUpdate = -1
      }
    }
    /* pull main to front */
    switchView(0)
  }
}

/*
 * removes the current profile
 */
function remProf () {
  var id = document.getElementById('profiles').value
  if (id !== 0) {
    if (window.confirm('Remove Profile #' + id + '?')) {
      sendCMD(0x18, id)
      doUpdate = -1
    }
  }
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

function setsmallUI () {
  enableElement('pscroll', !smallUI)
  enableElement('nscroll', !smallUI)
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
  setsmallUI()
  adaptUI()
}

function blockSpace (event) {
  /* only do this if the main or playlist view is visible! */
  if ((document.getElementById('extra0').className !== 'active') &&
      (document.getElementById('extra3').className !== 'active')) {
    return
  }
  if (event.key === ' ') {
    event.preventDefault()
  }
}

function handleKey (event) {
  /* only do this if the main or playlist view is visible! */
  if ((document.getElementById('extra0').className !== 'active') &&
      (document.getElementById('extra3').className !== 'active')) {
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
/*
 * start the UI update thread loops
 */
function initializeUI () {
  /* todo: attach event listeners here and not in HTML */
  if (window.innerWidth < window.innerHeight * 1.2) {
    switchView(3)
  }

  initScrolls()
  setsmallUI()
  updateUI()
  scrollToggle()
}
