if (window.getSelection) {
  var url = window.getSelection().toString();
  if (url != '') {
    // to re-enable as soon as the whole thing supports HTTPS
    // var xmlhttp = new window.XMLHttpRequest()
    // xmlhttp.open('POST', '%s:%s/mpctrl/cmd?' + JSON.stringify({ cmd: 0x17, arg: url, clientid: 0 }), true)
    // xmlhttp.send()
    // window.alert('Sent')
    location.assign('%s:%s/mpctrl/cmd?' + JSON.stringify({ cmd: 0x17, arg: url, clientid: 0 }))
  } else {
    window.alert('Nothing selected!')
  }
}
