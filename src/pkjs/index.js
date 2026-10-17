// Fetches the current temperature from Open-Meteo (no API key needed) and
// sends it to the watch. Runs on startup and whenever the watch asks.

function sendTemperature(pos) {
  var url = 'https://api.open-meteo.com/v1/forecast?' +
      'latitude=' + pos.coords.latitude +
      '&longitude=' + pos.coords.longitude +
      '&current=temperature_2m';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    var json = JSON.parse(this.responseText);
    Pebble.sendAppMessage({ TEMPERATURE: Math.round(json.current.temperature_2m) },
      function () { console.log('Temperature sent'); },
      function () { console.log('Sending temperature failed'); });
  };
  xhr.open('GET', url);
  xhr.send();
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(sendTemperature,
    function () { console.log('Location unavailable'); },
    { timeout: 15000, maximumAge: 60000 });
}

Pebble.addEventListener('ready', getWeather);

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.REQUEST_WEATHER) getWeather();
});
