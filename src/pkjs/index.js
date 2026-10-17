// Fetches the current temperature and today's sunrise/sunset from Open-Meteo
// (no API key needed) and sends them to the watch. Runs on startup and
// whenever the watch asks.

// "2026-10-17T07:58" (local time at the location) -> minutes after midnight
function minutesOfDay(iso) {
  return parseInt(iso.substr(11, 2), 10) * 60 + parseInt(iso.substr(14, 2), 10);
}

function sendWeather(pos) {
  var url = 'https://api.open-meteo.com/v1/forecast?' +
      'latitude=' + pos.coords.latitude +
      '&longitude=' + pos.coords.longitude +
      '&current=temperature_2m&daily=sunrise,sunset&timezone=auto&forecast_days=1';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    var json = JSON.parse(this.responseText);
    Pebble.sendAppMessage({
      TEMPERATURE: Math.round(json.current.temperature_2m),
      SUNRISE: minutesOfDay(json.daily.sunrise[0]),
      SUNSET: minutesOfDay(json.daily.sunset[0])
    },
      function () { console.log('Weather sent, sun ' + json.daily.sunrise[0] + ' - ' + json.daily.sunset[0]); },
      function () { console.log('Sending weather failed'); });
  };
  xhr.open('GET', url);
  xhr.send();
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(sendWeather,
    function () { console.log('Location unavailable'); },
    { timeout: 15000, maximumAge: 60000 });
}

Pebble.addEventListener('ready', getWeather);

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.REQUEST_WEATHER) getWeather();
});
