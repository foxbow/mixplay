/**
 * used cmd's so far are:
 *  mpc_play,
 *  mpc_prev,
 *  mpc_next,
 *  mpc_favtitle,
 *  mpc_repl,
 *  mpc_dnptitle,
 *	mpc_ivol,
 *	mpc_dvol,
 */
function sendCMD( cmd ) {
  alert( "Sending "+cmd );
}


function updateUI() {
}


/*
<div id='main' style='font-size: 250%' onselectstart='return false'>
       <input id='prev' type='button' onclick='playPrev()' value='prev' >
       <div id='artist'>artist</div>
       <input id='title' type='button' onclick='playPause()' value='title' >
       <div id='album'>album</div>
       <input id='prev' type='button' onclick='playNext()' value='next' >
       <input id='fav' type='button' onclick='markFAV()' value='DNP' >
       <input id='fav' type='button' onclick='markDNP()' value='FAV' >
       
       <div id='playtime'><pre>00:00 / 00:00</pre></div>
       <div id='volpack'>
         <input id='dvol' type='button' onclick='dvol()' value='-' >
         <div id='volume'>100%</div>
         <input id='ivol' type='button' onclick='ivol()' value='+' >
*/

