/* -1: configuration; 0: no update; 1: normal updates */
var doUpdate=-1;
var data=null;
var mpver=~~MPCOMM_VER~~;
var serverver="~~MIXPLAY_VER~~";
var isstream=0;
var msglines=["","","","","","","","","","","","","","",""];
var msgpos=0;
var active=0;
var scrolls=[];
var numscrolls=0;
var wasstream=-1;
var favplay=0;

/*
 * TODO: update searchresults on the fly
 */
function toggleSearch() {
  sendCMD(0x0019);
}

/*
 * pops up the current build ID
 */
function showInfo() {
	alert( "mixplayd "+serverver+" ("+mpver+")" );
}

/*
 * check if a long text needs to be scrolled and then scroll into the
 * proper direction
 */
function scrollToggle( ) {
	var to=1000;
	if (doUpdate == 0) return;
	for (var i = 0; i < numscrolls; i++) {
		var element=scrolls[i].element;
		if( scrolls[i].offset.charAt(0) == '-' ) {
			if( element.style.right == scrolls[i].offset ) {
				element.style.right="0px";
				to=10000;
			}
			else {
				element.style.right = scrolls[i].offset;
				to=15000;
			}
		}
	}

	setTimeout( "scrollToggle()", to );
}

/*
 * enables scrolling on texts that are longer than the parent container
 * and centers shorter texts
 */
function setScrolls( ) {
	/* only do this if the main view is visible! */
	if( document.getElementById( "extra0" ).style.display=='none' ) {
		return;
	}

	for( var i=0; i < numscrolls; i++ ) {
		var scroll=scrolls[i];
		var element=scroll.element;
		element.style.left="auto";
		element.style.right="auto";
		var off_right=getComputedStyle(element).right;
		var right=parseInt(off_right,10);
		scroll.offset=off_right;
		if( off_right.charAt(0) == '-' ) {
			element.style.right = off_right;
			element.offsetHeight;
			element.style.transition = 'right 5s ease-in-out';
		}
		else {
			element.style.left=(right/2)+"px";
		}
	}
}

/*
 * sets up the scroller object
 */
function initScroll(index,id) {
	if( scrolls[index] == undefined ) {
		var element=document.getElementById(id);
		var scroll={"id":id,"element":element,"offset":"0px"};
		scrolls[index]=scroll;
    if( index+1 > numscrolls ) {
      numscrolls=index+1;
    }
	}
}

/*
 * start scrollers onload - see HTML
 */
function initScrolls() {
	initScroll(0, 'prev');
	initScroll(1, 'title');
  initScroll(2, 'next');
  initScroll(3, 'artist');
  initScroll(4, 'album');
}

/*
 * stop updates and make sure that error messages do not stack
 */
function fail( msg ) {
	if( doUpdate != 0 ) {
		doUpdate=0;
		/* pull main to front */
		alert( msg );
	}
}

/*
 * set profile to sanitized value
 */
function setProf() {
	var e=document.getElementById("profiles");
	var id=e.value;
	if( id != 0 ) {
		/* pull main to front */
		toggleVisibility('0');
		sendCMD( 0x06,id );
	}
	else {
		e.value=active;
	}
}

/*
 * toggle named tab, usually caleld by toggleTab() but also used
 * to set active tab explicitly
 */
function toggleTabByRef( element, num ) {
	var i=0;
	var b;
	var e=document.getElementById( element+i );

	while( e!=null ) {
		b=document.getElementById( "c"+element+i );
		if( i == num ) {
			b.style.backgroundColor='#ddd';
			e.style.display='block';
		}
		else {
			/* special case for hidden tabs */
			if( b != null ) {
				b.style.backgroundColor='#fff';
			}
			e.style.display='none';
		}
		i=i+1;
		e=document.getElementById( element+i );
	}
}

/**
 * toggle tabified result tabs
 */
 function toggleTab( ref ) {
   var name=ref.id.substring(1,ref.id.length-1);
   var num=parseInt(ref.id.substring(ref.id.length-1));
   toggleTabByRef( name, num );
 }

/*
 * toggle main UI tabs
 * TODO: use toggleTab() and call setScrolls() on changeVisibility hook
 */
function toggleVisibility( element ) {
	toggleTabByRef( "extra", element );
	if( element == '0' ) {
		setScrolls();
	}
}

/*
 * stop the server - this should really not be here =)
 */
function killServer() {
	if( confirm("Do you really want to stop the Server?") ) {
		toggleVisibility('4');
		sendCMD(0x11);
	}
}

/*
 * add a line of text to the message pane. Acts as ringbuffer
 */
function addText(text) {
	var line="";
	var numlines=15;
	e=document.getElementById('extra4');
  document.getElementById('cextra4').style.display='inline';

	if( msgpos < numlines ) {
		msglines[msgpos]=text;
		msgpos++;
	}
	else {
		for( i=0; i<numlines-1; i++ ) {
			msglines[i]=msglines[i+1];
		}
		msglines[numlines-1]=text;
	}

	for( i=0; i<numlines; i++ ) {
		line+=msglines[i]+"<br>\n";
	}
	e.innerHTML=line;
}

/*
 * send a command with optional argument to the server
 */
function sendCMD( cmd, arg="" ) {
	var xmlhttp=new XMLHttpRequest();
	var code=Number(cmd).toString(16);
	while (code.length < 4) {
		code = "0" + code;
	}

	/* filter out commands that make no sense in stream */
	if( ( isstream ) && (
		( code == '0002' ) ||
		( code == '0003' ) ||
		( code == '0005' ) ||
		( code == '000f' ) ||
		( code == '0010' ) ||
    ( code == '001e' ) ) ) return;

	/* these commands should pull main to front */
	if( ( code == '001e' ) ) toggleVisibility('0');

	/* These command should pull the messages to front */
	if( ( code == '0008' ) ||
	    ( code == '0012' ) ) toggleVisibility('4');

	xmlhttp.onreadystatechange=function() {
		if ( xmlhttp.readyState==4  ) {
			switch( xmlhttp.status ) {
			case 0:
				fail( "CMD Error: connection lost!" );
				break;
			case 204:
				break;
			case 503:
				alert( "Sorry, we're busy!" );
				break;
			default:
				fail( "Received Error "+xmlhttp.status+" after sending 0x"+code );
			}
		}
	}

	if( arg != "" ) {
		xmlhttp.open("GET", "/cmd/"+code+"?"+arg, true);
	}
	else {
		xmlhttp.open("GET", "/cmd/"+code, true);
	}
	xmlhttp.send();
}

/*
 * use scrollwheel to control volume
 */
function volWheel( e ) {
	if( e.deltaY < 0 ) {
		sendCMD( 0x0d);
	}
	else if( e.deltaY > 0 ) {
		sendCMD( 0x0e );
	}
}

/*
 * change text in an element
 */
function setElement( e, val ) {
	document.getElementById( e ).innerHTML=val;
}

/*
 * show/hide a <div>
 */
function setVisible( e, i ) {
	if( i == 0 ) {
		document.getElementById( e ).style.display='none';
	}
	else {
		document.getElementById( e ).style.display='block';
	}
}

/*
 * show/hide an inline element
 */
function enableTab( e, i ) {
	if( i == 0 ) {
		document.getElementById( e ).style.display='none';
	}
	else {
		document.getElementById( e ).style.display='inline';
	}
}

function getPattern( line, cmd ) {
	var encline=line;
	var reply="<p class='cmd' onclick='this.style.display=\"none\"; sendCMD( "+cmd+", \""+encodeURI(encline)+"\")'>";

	switch( line.charAt(0) ) {
		case 't':
		case 'd':
			reply+="Title ";
		break;
		case 'a':
			reply+="Artist ";
		break;
		case 'l':
			reply+="Album ";
		break;
		case 'g':
			reply+="Genre ";
		break;
		case 'p':
			reply+="Path ";
		break;
		default:
			reply+=line.charAt(0)+"? ";
		break;
	}
	switch( line.charAt(1) ) {
		case '*':
			reply+="~ ";
		break;
		case '=':
			reply+="= ";
		break;
		default:
			reply+=line.charAt(1)+"? ";
		break;
	}
	reply+=line.substring(2);
	reply+="</p>\n"
	return reply;
}

/*
 * creates a string containing a line that disappers
 * in click and calls cmd with arg
 * unfortunately the onclick is not easy to realize in a DOM object
 */
 function clickline( cmd, arg, text ) {
   var p="<p class='cmd' ";
   p+="onclick='this.style.display=\"none\"; sendCMD( "+cmd+", \""+arg+"\" )'>";
   p+=text;
   p+="</p>\n";
   return p;
 }

 function clickselect( cmd, cmd2, arg ) {
   if( confirm("Mark as favourite?\nCancel will search") ) {
     sendCMD( cmd, arg );
   }
   else {
     sendCMD( cmd2, arg );
   }
 }

 function clickline2( cmd, cmd2, arg, text ) {
   var p="<p class='cmd' ";
   p+="onclick='this.style.display=\"none\"; clickselect( "+cmd+", "+cmd2+", \""+arg+"\" )'>";
   p+=text;
   p+="</p>\n";
   return p;
 }

/*
 * parent: parent container to put list in
 * name: unique name for tab control
 * list: array of DOM elements to tabify
 */
function tabify( parent, name, list ) {
  var num=list.length;
  var tabs=parseInt(num/20);
  if( ( tabs % 20 ) == 0  ) {
    tabs--;
  }
  while( parent.hasChildNodes() ) {
    parent.removeChild(parent.firstChild);
  }
  if (tabs > 0 ) {
    for( i=0; i<=tabs; i++ ) {
      tabswitch=document.createElement('input');
      tabswitch.id="c"+name+i;
      tabswitch.className='cmd';
      tabswitch.type='button';
      tabswitch.onclick=function(){toggleTab(this);};
      tabswitch.value='['+i+']';
      if( i==0 ) {
        tabswitch.style.backgroundColor='#ddd';
      }
      parent.appendChild(tabswitch);
    }
    for( i=0; i<=tabs; i++ ) {
      if( tabs > 0 ) {
        tabdiv=document.createElement('div');
        tabdiv.id=name+i;
        if(i==0){
          tabdiv.style.display="block";
        }
        else {
          tabdiv.style.display="none";
        }
        tabdiv.width="100%";
        parent.appendChild(tabdiv);
      }
      for( j=0; (j<20) && (20*i+j < num ); j++ ) {
        tabdiv.innerHTML+=list[20*i+j];
      }
    }
  }
  else {
    for( i=0; i < num; i++ ) {
      parent.innerHTML+=list[i];
    }
  }
}

/*
 * get current status from the server and update the UI elements with the data
 * handles different types of status replies
 */
function updateUI( ){
	var xmlhttp=new XMLHttpRequest();
	xmlhttp.onreadystatechange=function() {
    var e;
    var items;
	if (xmlhttp.readyState==4 ) {
		if( xmlhttp.status==200 ) {
			var data=JSON.parse(xmlhttp.responseText);
			if( data !== undefined ) {
				if( data.version != mpver ) {
					fail( "Version clash, expected "+mpver+" and got "+data.version );
					return;
				}
				/* full update */
				if( data.type & 1 ) {
					e=document.getElementById('plist');
					e.innerHTML="";
					document.title=data.current.artist+" - "+data.current.title;
					if( data.prev.length > 0 ) {
	              		if( data.mpmode == 1 ) {
	                	/* only the stream title */
	                		setElement( 'prev', data.prev[0].title );
	              		}
	              		else {
							setElement( 'prev', data.prev[0].artist+" - "+data.prev[0].title );
	              		}
						for( i = Math.min( 4, data.prev.length-1 ); i >= 0 ; i-- ) {
							e.innerHTML+="<p class='cmd' onclick='sendCMD( 0x0002, "+(i+1)+")'>"+data.prev[i].artist+" - "+data.prev[i].title+"</p>";
						}
					}
					else {
						setElement( 'prev', "- - -" );
					}
					setElement( 'title', data.current.title );
					setElement( 'artist', data.current.artist );
					setElement( 'album', data.current.album );
					setElement( 'genre', data.current.genre );
					e.innerHTML+="<p class='cmd' onclick='sendCMD( 0x0000 )' style='background-color: #ddd;' ><em>"+data.current.artist+" - "+data.current.title+"</em></p>";
					if( data.next.length > 0 ) {
		          		if( data.next[0].artist == "" ) {
		            		/* only the stream title */
		            		setElement( 'next', data.next[0].title );
		          		}
		          		else {
							setElement( 'next', data.next[0].artist+" - "+data.next[0].title );
		          		}
						for( i=0; i<data.next.length; i++ ) {
	              			e.innerHTML+="<input style='width:2em; border:none; background:none; float:left; color:red;' type='button' onclick='sendCMD( 0x1c, "+data.next[i].key+" )' value='X'>"; 
	              			titleline="<p class='cmd' onclick='sendCMD( 0x0003, "+(i+1)+")'>";
	              			if( data.next[i].playcount >= 0 ) {
								titleline+="["+data.next[i].playcount+"] ";
							}
							titleline+=data.next[i].artist+" - "+data.next[i].title+"</p>";
							e.innerHTML+=titleline;
						}
					}
					else {
						setElement( 'next', "- - -" );
					}
					if( data.current.flags & 1 )  {
						document.getElementById( 'fav' ).disabled=true;
					}
					else {
						document.getElementById( 'fav' ).disabled=false;
					}
					setScrolls();
				}

				/* search results */
				if( data.type & 2 ) {
					toggleTabByRef("search", 0);
					e=document.getElementById('search0');
		            items=[];
					if( data.titles.length > 0 ) {
						if( data.mpedit ) {
							items[0]="<a href='/cmd/0114?0'>Fav all</a><br/>";
						}
						else {
							items[0]="<a href='/cmd/010c?0'>Play all</a><br/>";
						}
						for( i=0; i<data.titles.length; i++ ){
		              		if( data.mpedit ) {
                  				items[i+1]=clickline( 0x0809, data.titles[i].key, "&hearts; "+data.titles[i].artist+" - "+data.titles[i].title );
							}
        			        else {
                	  			items[i+1]=clickline( 0x080c, data.titles[i].key, "&#x25B6; "+data.titles[i].artist+" - "+data.titles[i].title );
                			}
              			}
					}
					else {
						items[0]="<em>No titles found!</em>";
					}
		            tabify( e, "tres", items );
					e=document.getElementById('search1');
        		    items=[];
					if( data.artists.length > 0 ) {
						for( i=0; i<data.artists.length; i++ ) {
        			        if( data.mpedit ) {
        			        	items[i]=clickline2( 0x0209, 0x0213, data.artists[i], "&hearts; "+data.artists[i] );
                			}
                			else {
			                	items[i]=clickline( 0x0213, data.artists[i], "&#x1F50E; "+data.artists[i] );
                			}
						}
					}
					else {
						items[0]="<em>No artists found!</em>";
					}
		            tabify( e, "ares", items );
						e=document.getElementById('search2');
            			items=[];
						if( data.albums.length > 0 ) {
							for( i=0; i<data.albums.length; i++ ) {
                				if( data.mpedit ) {
                  					items[i]=clickline2( 0x0409, 0x0413, data.albums[i], "&hearts; "+data.albart[i]+" - "+data.albums[i] );
                				}
                				else {
                  					items[i]=clickline( 0x0413, data.albums[i], "&#x1F50E; "+data.albart[i]+" - "+data.albums[i] );
                				}
							}
						}
						else {
							items[0]="<em>No albums found!</em>";
						}
			            tabify( e, "lres", items );
						if( data.titles.lenght == 0 ) {
							alert( "Search found "+data.artists.length+" artists and "+data.albums.length+" albums" );
						}
					}

					/* dnp/fav lists */
					if( data.type & 4 ) {
						e=document.getElementById("search3");
            			items=[];
						if( data.dnplist.length == 0 ) {
							items[0]="<em>No DNPs yet</em>";
						}
						else {
							for( i=0; i<data.dnplist.length; i++) {
								items[i]=getPattern(data.dnplist[i],"0x001a");
							}
						}
            			tabify( e, "dlist", items );

						e=document.getElementById("search4");
            			items=[];
						if( data.favlist.length == 0 ) {
							items[0]="<em>No Favourites yet</em>";
						}
						else {
							for( i=0; i<data.favlist.length; i++) {
								items[i]=getPattern(data.favlist[i],"0x001b");
							}
						}
            			tabify( e, "flist", items );
					}

					/* standard update */
					if( data.mpedit ) {
						document.getElementById('searchmode').value="Fav";
          			}
          			else {
			            document.getElementById('searchmode').value="Play";
          			}
			        favplay=data.mpfavplay;
			        if( favplay ) {
            			document.getElementById('setfavplay').value="Disable Favplay";
            			document.getElementById('range').selectedIndex=0;
			        }
			        else {
				        document.getElementById('setfavplay').value="Enable Favplay";
			        }
					isstream=( data.mpmode == 1 ); /* PM_STREAM */

			        setVisible( 'fav', !favplay );
			        setVisible( 'range', !favplay );
			        setVisible( 'setfavplay', !isstream );
					setVisible( 'ctrl', !isstream );
					setVisible( 'playstr', isstream );
					setVisible( 'playpack', !isstream );
					enableTab( 'cextra1', !isstream );

			        /* switching between stream and normal play */
					if( isstream ) {
						setElement( 'splaytime', data.playtime );
        			    document.getElementById( 'lscroll' ).style.height='0px';
					}
					else {
						setElement( 'playtime', data.playtime );
						setElement( 'remtime', data.remtime );
						document.getElementById( 'progress' ).value=data.percent;
			            document.getElementById( 'lscroll' ).style.height='30px';
					}

			        if( ( isstream != wasstream ) &&
            			  ( window.innerHeight != window.outerHeight ) ) {
			            if( isstream ) {
              				window.resizeTo( 300, 250 );
			            }
			            else {
				            window.resizeTo( 300, 380 );
			            }
			        }
			        wasstream=isstream;

					if( active != data.active ) {
						active=data.active;
						setActive( active );
					}
					if( data.status == 0 ) {
						document.getElementById('current').style.backgroundColor="#ddd";
					}
					else {
						document.getElementById('current').style.backgroundColor="#daa";
					}

			        if(data.volume==-2) {
            			document.getElementById('speaker').innerHTML='&#x1F507;';
			        }
          			else {
				        document.getElementById('speaker').innerHTML='&#x1F50A;';
						document.getElementById( 'vol' ).value=data.volume;
			        }
			        if( data.mpedit == true ) {
            			document.body.style.backgroundColor='#ddf'
			        }
			        else {
            			document.body.style.backgroundColor='#fff'
			        }

					if( ( data.msg != "" ) && ( data.msg != "Done." ) ) {
						addText(data.msg);
					}
				}
			}
			else if( xmlhttp.status == 0 ) {
				fail( "Update Error: connection lost!" );
			}
			else {
				fail( "Received Error "+xmlhttp.status+" after sending 0x"+code );
			}

			if( doUpdate != 0 ) {
				setTimeout("updateUI()",750);
			}
			else {
				document.body.style.backgroundColor='#daa'
				document.getElementById('current').style.backgroundColor="#daa";
			}
		}
	}

	if( doUpdate == -1 ) {
		setTimeout("getConfig()",333);
		doUpdate=1;
	}

	xmlhttp.open("GET", "/status", true);
	xmlhttp.send();
}

/*
 * shows the current profile/channel in the profile select dropdown
 */
function setActive( id ) {
	var s=document.getElementById("profiles");
	s.value=id;
}

/*
 * gets the basic configuration of the server
 * this should only happen once at start
 */
function getConfig() {
	var xmlhttp=new XMLHttpRequest();
	xmlhttp.onreadystatechange=function() {
		if (xmlhttp.readyState==4 ) {
			if( xmlhttp.status==200 ) {
				data=JSON.parse(xmlhttp.responseText);
				if( data !== undefined ) {
					if( data.version != mpver ) {
						fail( "Version clash, expected "+mpver+" and got "+data.version );
						return;
					}
					if( data.type == -1 ) {
						var s=document.getElementById("profiles");
						s.options.length=0;
						for(i=0; i<data.config.profiles; i++) {
							s.options[s.options.length] = new Option(data.config.profile[i],i+1);
						}
						s.options[s.options.length] = new Option("None",0);
						for(i=0; i<data.config.streams; i++) {
							s.options[s.options.length] = new Option(data.config.sname[i],-(i+1));
						}
						active=0;
					}
					else {
						fail( "Received reply of type "+data.type+" for config!" );
					}
				}
				else {
					fail( "Received no Data for config!" );
				}
			}
			else {
				fail( "Received Error "+xmlhttp.status+" trying to get config" );
			}
		}
	}

	xmlhttp.open("GET", "/config", true);
	xmlhttp.send();
}

/*
 * send command with range info (FAV/DNP)
 */
function sendRange( cmd, term="" ) {
	var e=document.getElementById('range');
	if( term != "" ) {
		e=document.getElementById('srange');
	}
	var range=e.options[e.selectedIndex].value;
	if( isstream ) return;
	cmd|=range;
	sendCMD( cmd, term );
}

/*
 * send command with argument set in the 'text' element (Search)
 */
function sendArg( cmd ) {
	if( isstream ) return;
	var term=document.getElementById('text').value;
	if( term.length > 1 ) {
		sendRange( cmd, term, 1 );
	}
	else {
		alert("Need at least two letters!");
	}
}

/*
 * creates a new profile or loads a channel
 */
function createLoad() {
	var term=document.getElementById('ptext').value;
	var asking="";
	if( term.length < 3 ) {
		alert("Need at least two letters!");
	}
	else {
		if( term.toLowerCase().startsWith("http") ) {
			sendCMD( 0x17, term );
		}
		else {
			if( isstream ) {
				asking="Add current stream as channel "+term+" ?"
			}
			else {
				asking="Create new profile "+term+" ?"
			}
			if( confirm( asking ) ) {
				sendCMD( 0x16, term );
				doUpdate=-1;
			}
		}
		/* pull main to front */
		toggleVisibility('0');
	}
}

/*
 * removes the current profile
 */
function remProf() {
	var id=document.getElementById("profiles").value;
	if( id != 0 ) {
		if( confirm( "Remove Profile #"+id+"?" ) ) {
			sendCMD( 0x18,id );
			doUpdate=-1;
		}
	}

}

/*
 * download a title by key (0=current)
 */
function download(key=0) {
	if( confirm( "Download "+document.getElementById('title').innerHTML+" ?" ) ) {
		window.location="/title/"+key;
	}
}

/*
 * start the UI update thread loops
 */
updateUI();
scrollToggle();
