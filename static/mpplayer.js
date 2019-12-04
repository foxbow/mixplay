function getInfo () {
  var xmlhttp = new document.XMLHttpRequest()
  xmlhttp.onreadystatechange = function () {
    if (xmlhttp.readyState === 4) {
      if (xmlhttp.status === 200) {
        document.title = xmlhttp.responseText
        document.getElementById('title').innerHTML = xmlhttp.responseText
      } else if (xmlhttp.status === 0) {
        document.alert('Update Error: connection lost!')
      }
    }
  }

  xmlhttp.open('GET', '/mpctrl/title/info', true)
  xmlhttp.send()
}

function next () {
  var e = document.getElementById('player')
  e.load()
  getInfo()
}

next()
