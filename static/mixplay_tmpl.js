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
var wasstream = -1
var favplay = 0

function replaceChild (e, c) {
  while (e.hasChildNodes()) {
    e.removeChild(e.firstChild)
  }
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
 * enables scrolling on texts that are longer than the parent container
 * and centers shorter texts
 */
function setScrolls () {
  /* only do this if the main view is visible! */
  if (document.getElementById('extra0').style.display === 'none') {
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
    var scroll = { 'id': id, 'element': element, 'offset': '0px' }
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
    /* pull main to front */
    window.alert(msg)
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
    toggleVisibility(0)
    sendCMD(0x06, id)
  } else {
    e.value = active
  }
}

/*
 * toggle named tab, usually called by toggleTab() but also used
 * to set active tab explicitly
 */
function toggleTabByRef (element, num) {
  var i = 0
  var b
  var e = document.getElementById(element + i)

  while (e !== null) {
    b = document.getElementById('c' + element + i)
    if (i === num) {
      b.style.backgroundColor = '#ddd'
      e.style.display = 'block'
    } else if (b !== null) {
      b.style.backgroundColor = '#fff'
      e.style.display = 'none'
    }
    i++
    e = document.getElementById(element + i)
  }
}

/**
 * toggle tabified result tabs
 */
function toggleTab (ref) {
  var name = ref.id.substring(1, ref.id.length - 1)
  var num = parseInt(ref.id.substring(ref.id.length - 1))
  toggleTabByRef(name, num)
}

/*
 * toggle main UI tabs
 * TODO: use toggleTab() and call setScrolls() on changeVisibility hook
 */
function toggleVisibility (element) {
  var e
  if (element === 4) {
    e = document.getElementById('cextra4')
    e.value = '\u2713'
    e.style.display = 'inline'
  }
  toggleTabByRef('extra', element)
  if (element === 0) {
    setScrolls()
  }
}

/*
 * stop the server
 */
function killServer () {
  var reply = window.prompt('Really stop?')
  if ((reply !== null) && (reply !== '')) {
    sendCMD(0x07, reply)
  }
}

/*
 * add a line of text to the message pane. Acts as ringbuffer
 */
function addText (text) {
  var line = ''
  var numlines = 15
  var e = document.getElementById('extra4')
  if (e.style.display === 'none') {
    enableButton('cextra4', 1)
    document.getElementById('cextra4').value = '\u26A0'
  }

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
    line += msglines[i] + '<br>\n'
  }
  e.innerHTML = line
}

function wipeLog () {
  var i
  var e = document.getElementById('extra4')
  enableButton('cextra4', 0)
  for (i = 0; i < 15; i++) {
    msglines[i] = ''
  }
  e.innerHTML = ''
}

/*
 * send a command with optional argument to the server
 */
function sendCMD (cmd, arg = '') {
  var xmlhttp = new XMLHttpRequest()
  var code = Number(cmd).toString(16)
  while (code.length < 4) {
    code = '0' + code
  }

  /* filter out some commands that make no sense in stream */
  if ((isstream) && (
    (code === '0002') ||
    (code === '0003') ||
    (code === '0005') ||
    (code === '0005') ||
    (code === '0008') ||
    (code === '0009') ||
    (code === '000a') ||
    (code === '000b') ||
    (code === '000c') ||
    (code === '000f') ||
    (code === '0010') ||
    (code === '0011') ||
    (code === '0012') ||
    (code === '0013') ||
    (code === '0014') ||
    (code === '0019') ||
    (code === '001c') ||
    (code === '001e'))) return

  /* these commands should pull main to front */
  if (code === '001e') toggleVisibility(0)

  /* These command should pull the messages to front */
  if ((code === '0008') ||
     (code === '0007') ||
     (code === '0012')) toggleVisibility(4)

  xmlhttp.onreadystatechange = function () {
    if (xmlhttp.readyState === 4) {
      switch (xmlhttp.status) {
        case 0:
          fail('CMD Error: connection lost!')
          break
        case 204:
          break
        case 503:
          window.alert('Sorry, we\'re busy!')
          break
        default:
          fail('Received Error ' + xmlhttp.status + ' after sending 0x' + code)
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
 * show/hide a <div>
 */
function enableDiv (e, i) {
  if (i) {
    document.getElementById(e).style.display = 'block'
  } else {
    document.getElementById(e).style.display = 'none'
  }
}

/*
 * show/hide an inline element
 */
function enableButton (e, i) {
  if (i) {
    document.getElementById(e).style.display = 'inline'
  } else {
    document.getElementById(e).style.display = 'none'
  }
}

function cmdline (cmd, arg, text) {
  var reply = document.createElement('p')
  reply.className = 'cmd'
  reply.setAttribute('data-arg', arg)
  reply.setAttribute('data-cmd', cmd)
  reply.onclick = function () {
    sendCMD(this.getAttribute('data-cmd'), this.getAttribute('data-arg'))
  }
  reply.innerHTML = text
  return reply
}

/*
 * creates a string containing a line that disappers
 * on click and calls cmd with arg
 */
function clickline (cmd, arg, text) {
  var reply = document.createElement('p')
  reply.className = 'cmd'
  reply.setAttribute('data-arg', arg)
  reply.setAttribute('data-cmd', cmd)
  reply.onclick = function () {
    this.style.display = 'none'
    sendCMD(this.getAttribute('data-cmd'), this.getAttribute('data-arg'))
  }
  reply.innerHTML = text
  return reply
}

/**
 * wrapper to call clickline with a FAV/DNP line
 */
function getPattern (cmd, line) {
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
  return clickline(cmd, line, text)
}

/* creates a selection in a popselect popup */
function clickable (text, cmd, arg, popname) {
  var reply = document.createElement('em')
  reply.className = 'cmd'
  reply.setAttribute('data-arg', arg)
  reply.setAttribute('data-cmd', cmd)
  reply.onclick = function () {
    var popup = document.getElementById(popname)
    popup.classList.toggle('show')
    sendCMD(this.getAttribute('data-cmd'), this.getAttribute('data-arg'))
  }
  reply.innerHTML = text
  return reply
}

/* returns a <div> with text that when clicked presents the two choices */
function popselect (choice1, cmd1, arg1, choice2, cmd2, arg2, text) {
  var reply = document.createElement('p')
  reply.innerText = '> ' + text
  reply.className = 'popselect'
  reply.onclick = function () {
    var popup = document.getElementById('popup' + arg1)
    popup.classList.toggle('show')
  }
  var popspan = document.createElement('span')
  popspan.className = 'popup'
  popspan.id = 'popup' + arg1
  var select = clickable(choice1 + '&nbsp;/', cmd1, arg1, popspan.id)
  popspan.appendChild(select)
  select = clickable('&nbsp;' + choice2, cmd2, arg2, popspan.id)
  popspan.appendChild(select)
  reply.appendChild(popspan)
  return reply
}

/*
 * parent: parent container to put list in
 * name: unique name for tab control
 * list: array of DOM elements to tabify
 */
function tabify (parent, name, list) {
  var num = list.length
  var tabs = parseInt(num / 20)
  if ((tabs % 20) === 0) {
    tabs--
  }
  while (parent.hasChildNodes()) {
    parent.removeChild(parent.firstChild)
  }
  if (tabs > 0) {
    for (var i = 0; i <= tabs; i++) {
      var tabswitch = document.createElement('input')
      tabswitch.id = 'c' + name + i
      tabswitch.className = 'cmd'
      tabswitch.type = 'button'
      tabswitch.onclick = function () { toggleTab(this) }
      tabswitch.value = '[' + i + ']'
      if (i === 0) {
        tabswitch.style.backgroundColor = '#ddd'
      }
      parent.appendChild(tabswitch)
    }
    for (i = 0; i <= tabs; i++) {
      if (tabs > 0) {
        var tabdiv = document.createElement('div')
        tabdiv.id = name + i
        if (i === 0) {
          tabdiv.style.display = 'block'
        } else {
          tabdiv.style.display = 'none'
        }
        tabdiv.width = '100%'
        parent.appendChild(tabdiv)
      }
      for (var j = 0; (j < 20) && (20 * i + j < num); j++) {
        tabdiv.appendChild(list[20 * i + j])
      }
    }
  } else {
    for (i = 0; i < num; i++) {
      parent.appendChild(list[i])
    }
  }
}

/*
 * get current status from the server and update the UI elements with the data
 * handles different types of status replies
 */
function updateUI () {
  var xmlhttp = new XMLHttpRequest()
  xmlhttp.onreadystatechange = function () {
    var e
    var items
    if (xmlhttp.readyState === 4) {
      if (xmlhttp.status === 200) {
        var data = JSON.parse(xmlhttp.responseText)
        if (data !== undefined) {
          if (data.version !== mpver) {
            fail('Version clash, expected ' + mpver + ' and got ' + data.version)
            return
          }
          /* full update */
          if (data.type & 1) {
            e = document.getElementById('dnpfav0')
            while (e.hasChildNodes()) {
              e.removeChild(e.firstChild)
            }
            document.title = data.current.artist + ' - ' + data.current.title
            if (data.prev.length > 0) {
              if (data.mpmode === 1) {
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
                var cline = cmdline(0x0002, (i + 1), titleline + data.prev[i].artist + ' - ' + data.prev[i].title)
                e.appendChild(cline)
              }
            } else {
              setElement('prev', '- - -')
            }
            setElement('title', data.current.title)
            setElement('artist', data.current.artist)
            setElement('album', data.current.album)
            setElement('genre', data.current.genre)
            titleline = ''
            if (data.current.playcount >= 0) {
              titleline += '[' + data.current.playcount + '/' + data.current.skipcount + '] '
            }
            cline = cmdline(0x0000, '', '<em>' + titleline + data.current.artist + ' - ' + data.current.title + '</em>')
            cline.style.backgroundColor = '#ddd'
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
                cline = popselect('Play next', 0x11, data.next[i].key,
                  'Remove', 0x1c, data.next[i].key,
                  titleline)
                e.appendChild(cline)
              }
            } else {
              setElement('next', '- - -')
            }
            if (data.current.flags & 1) {
              document.getElementById('fav').disabled = true
            } else {
              document.getElementById('fav').disabled = false
            }
            setScrolls()
          }

          /* search results */
          if (data.type & 2) {
            toggleTabByRef('search', 0)
            e = document.getElementById('search0')
            items = []
            if (data.titles.length > 0) {
              if (data.mpedit) {
                items[0] = document.createElement('a')
                items[0].href = '/cmd/0114?0'
                items[0].innerHTML = 'Fav all'
              } else {
                items[0] = document.createElement('a')
                items[0].href = '/cmd/010c?0'
                items[0].innerHTML = 'Play all'
              }
              for (i = 0; i < data.titles.length; i++) {
                if (data.mpedit) {
                  items[i + 1] = clickline(0x0809, data.titles[i].key, '\u2665 ' + data.titles[i].artist + ' - ' + data.titles[i].title)
                } else {
                  items[i + 1] = clickline(0x080c, data.titles[i].key, '&#x25B6; ' + data.titles[i].artist + ' - ' + data.titles[i].title)
                }
              }
            } else {
              items[0] = document.createElement('em')
              items[0].innerHTML = 'No titles found!'
            }
            tabify(e, 'tres', items)

            e = document.getElementById('search1')
            items = []
            if (data.artists.length > 0) {
              for (i = 0; i < data.artists.length; i++) {
                if (data.mpedit) {
                  items[i] = popselect('Search', 0x0213, data.artists[i],
                    'Favourite', 0x0209, data.artists[i],
                    data.artists[i])
                } else {
                  items[i] = clickline(0x0213, data.artists[i], '&#x1F50E; ' + data.artists[i])
                }
              }
            } else {
              items[0] = document.createElement('em')
              items[0].innerHTML = 'No artists found!'
            }
            tabify(e, 'ares', items)

            e = document.getElementById('search2')
            items = []
            if (data.albums.length > 0) {
              for (i = 0; i < data.albums.length; i++) {
                if (data.mpedit) {
                  items[i] = popselect('Search', 0x0413, data.albums[i],
                    'Favourite', 0x0409, data.albums[i],
                    data.albart[i] + ' - ' + data.albums[i])
                } else {
                  items[i] = clickline(0x0413, data.albums[i], '&#x1F50E; ' + data.albart[i] + ' - ' + data.albums[i])
                }
              }
            } else {
              items[0] = document.createElement('em')
              items[0].innerHTML = 'No albums found!'
            }
            tabify(e, 'lres', items)

            if (data.titles.lenght === 0) {
              window.alert('Search found ' + data.artists.length + ' artists and ' + data.albums.length + ' albums')
            }
          }

          /* dnp/fav lists */
          if (data.type & 4) {
            e = document.getElementById('dnpfav1')
            items = []
            if (data.dnplist.length === 0) {
              items[0] = document.createElement('em')
              items[0].innerHTML = 'No DNPs yet'
            } else {
              for (i = 0; i < data.dnplist.length; i++) {
                items[i] = getPattern(0x001a, data.dnplist[i])
              }
            }
            tabify(e, 'dlist', items)

            e = document.getElementById('dnpfav2')
            items = []
            if (data.favlist.length === 0) {
              items[0] = document.createElement('em')
              items[0].innerHTML = 'No favourites yet'
            } else {
              for (i = 0; i < data.favlist.length; i++) {
                items[i] = getPattern(0x001b, data.favlist[i])
              }
            }
            tabify(e, 'flist', items)
          }

          /* standard update */
          if (data.mpedit) {
            document.getElementById('searchmode').value = 'Fav'
          } else {
            document.getElementById('searchmode').value = 'Play'
          }
          favplay = data.mpfavplay
          if (favplay) {
            document.getElementById('setfavplay').value = 'Disable Favplay'
            document.getElementById('range').selectedIndex = 0
          } else {
            document.getElementById('setfavplay').value = 'Enable Favplay'
          }
          isstream = (data.mpmode === 1) /* PM_STREAM */

          enableButton('fav', !favplay)
          enableButton('range', !favplay)
          enableButton('setfavplay', !isstream)
          enableDiv('ctrl', !isstream)
          enableDiv('playstr', isstream)
          enableDiv('playpack', !isstream)
          enableButton('cextra1', !isstream)

          /* switching between stream and normal play */
          if (isstream) {
            setElement('splaytime', data.playtime)
            document.getElementById('lscroll').style.height = '0px'
          } else {
            setElement('playtime', data.playtime)
            setElement('remtime', data.remtime)
            document.getElementById('progress').value = data.percent
            document.getElementById('lscroll').style.height = '30px'
          }

          if ((isstream !== wasstream) && (window.innerHeight !== window.outerHeight)) {
            if (isstream) {
              window.resizeTo(300, 250)
            } else {
              window.resizeTo(300, 380)
            }
          }
          wasstream = isstream

          if (active !== data.active) {
            active = data.active
            setActive(active)
          }
          if (data.status === 0) {
            document.getElementById('current').style.backgroundColor = '#ddd'
          } else {
            document.getElementById('current').style.backgroundColor = '#daa'
          }

          if (data.volume === -2) {
            document.getElementById('speaker').innerHTML = '&#x1F507;'
          } else {
            document.getElementById('speaker').innerHTML = '&#x1F50A;'
            document.getElementById('vol').value = data.volume
          }

          if (data.msg !== '') {
            if (data.msg.startsWith('ALERT:')) {
              window.alert(data.msg.substring(6))
            } else if (data.msg !== 'Done.') {
              addText(data.msg)
            }
          }
        }
      } else if (xmlhttp.status === 0) {
        fail('Update Error: connection lost!')
      } else {
        fail('Received Error ' + xmlhttp.status + ' after sending command')
      }

      if (doUpdate !== 0) {
        setTimeout(function () { updateUI() }, 750)
      } else {
        document.body.style.backgroundColor = '#daa'
        document.getElementById('current').style.backgroundColor = '#daa'
      }
    }
  }

  if (doUpdate === -1) {
    setTimeout(function () { getConfig() }, 333)
    doUpdate = 1
  }

  xmlhttp.open('GET', '/status', true)
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
 * send command with range info(FAV/DNP)
 */
function sendRange (cmd, term = '') {
  var e = document.getElementById('range')
  if (term !== '') {
    e = document.getElementById('srange')
  }
  var range = e.options[e.selectedIndex].value
  if (isstream) return
  cmd |= range
  sendCMD(cmd, term)
}

/*
 * send command with argument set in the 'text' element(Search)
 */
function sendArg (cmd) {
  if (isstream) return
  var term = document.getElementById('text').value
  if (term.length > 1) {
    sendRange(cmd, term, 1)
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
    toggleVisibility(0)
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

/*
 * start the UI update thread loops
 */
updateUI()
scrollToggle()
