function getInfo( ){
	var xmlhttp=new XMLHttpRequest();
	xmlhttp.onreadystatechange=function() {
		if (xmlhttp.readyState==4 ) {
			if( xmlhttp.status==200 ) {
				document.title=xmlhttp.responseText;
				document.getElementById( 'title' ).innerHTML=xmlhttp.responseText;
			}
			else if( xmlhttp.status == 0 ) {
				alert( "Update Error: connection lost!" );
			}
		}
	}

	xmlhttp.open("GET", "/title/info", true);
	xmlhttp.send();
}

function next() {
	var e=document.getElementById('player');
	e.load();
	getInfo();
}

