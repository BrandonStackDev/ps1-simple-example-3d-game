@REM I turn on pcsx-redux SIO1 server on port 3001 and then tail witht his command
@REM why you might ask? I dont understand but this is 
@REM the only way I could get the serial logging to work...
@REM better than nuffin' right?
@REM if you install nmap for windows you get ncat, linux just use nc or ncat

ncat localhost 3001