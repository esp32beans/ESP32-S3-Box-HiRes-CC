static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<title>Hi Res MIDI CC</title>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
.slidecontainer {
  width: 100%;
}

.slider {
  -webkit-appearance: none;
  width: 100%;
  height: 100px;
  background: #d3d3d3;
  outline: none;
  opacity: 0.7;
  -webkit-transition: .2s;
  transition: opacity .2s;
}

.slider:hover {
  opacity: 1;
}

.slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 100px;
  height: 100px;
  background: #04AA6D;
  cursor: pointer;
}

.slider::-moz-range-thumb {
  width: 100px;
  height: 100px;
  background: #04AA6D;
  cursor: pointer;
}
</style>
</head>
<body>

<h1>HiRes CC Slider</h1>

<div class="slidecontainer">
  <input type="range" min="0" max="16383" value="8192" class="slider" id="modulation">
  <p>Modulation: <span id="modulation_value"></span></p>
  <input type="range" min="0" max="16383" value="8192" class="slider" id="volume">
  <p>Volume: <span id="volume_value"></span></p>
</div>

<script>
const WS_OPEN = 1;
const WS_CLOSED = 3;
var websock;
var websockQueue = [];
var connected = false;

function websock_send(json) {
  if (websock.readyState === WS_OPEN) {
      websock.send(json);
  } else {
    if (websock.readyState === WS_CLOSED) {
        websock = new WebSocket('ws://' + window.location.hostname + ':81/');
    }
    websockQueue.push(json);
  }
}

var slider = document.getElementById("modulation");
var output = document.getElementById("modulation_value");
output.innerHTML = slider.value;

slider.oninput = function() {
  websock_send(JSON.stringify({name:this.id, hrcc_value:parseInt(this.value, 10)}));
  output.innerHTML = this.value;
}

var volume = document.getElementById("volume");
var volume_value = document.getElementById("volume_value");
volume_value.innerHTML = volume.value;

volume.oninput = function() {
  websock_send(JSON.stringify({name:this.id, hrcc_value:parseInt(this.value, 10)}));
  volume_value.innerHTML = this.value;
}

//var pitchbend = document.getElementById("pitchbend");
//var pb_value = document.getElementById("pb_value");
//pb_value.innerHTML = pitchbend.value;
//
//pitchbend.oninput = function() {
//  websock_send(JSON.stringify({name:this.id, hrcc_value:parseInt(this.value, 10)}));
//  pb_value.innerHTML = this.value;
//}

function start() {
  websock = new WebSocket('ws://' + window.location.hostname + ':81/');
  websock.onopen = function(evt) {
    console.log('websock onopen', evt);
    connected = true;
    for (var i = 0; i < websockQueue.length; i++) {
      websock.send(websockQueue[i]);
    }
    websockQueue = [];
  };
  websock.onclose = function(evt) {
    console.log('websock onclose', evt);
    connected = false;
  };
  websock.onerror = function(evt) {
    console.log('websock onerror', evt);
    connected = false;
  };
  websock.onmessage = function(evt) {
    console.log('websock onmessage', evt);
  };
}

document.addEventListener("DOMContentLoaded", start);
</script>

</body>
</html>
)rawliteral";
