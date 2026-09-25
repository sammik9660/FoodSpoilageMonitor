// ================================
// FOOD SPOILAGE MONITOR
// Google Apps Script Backend
// ================================


// --------------------------------
// 설정
// --------------------------------

const SHEET_NAME = "SensorData";


// --------------------------------
// ESP32 → POST
// --------------------------------

function doPost(e) {

  try {

    const data = JSON.parse(e.postData.contents);

    const sensorData = {
      temperature: Number(data.temperature),
      humidity: Number(data.humidity),
      pressure: Number(data.pressure),
      gas: Number(data.gas),
      timestamp: new Date().toISOString()
    };


    // --------------------------------
    // 1. 최신 센서값 저장
    // --------------------------------

    PropertiesService
      .getScriptProperties()
      .setProperty(
        "SENSOR_DATA",
        JSON.stringify(sensorData)
      );


    // --------------------------------
    // 2. Google Sheets에 누적 저장
    // --------------------------------

    const sheet = getSensorSheet();

    sheet.appendRow([
      new Date(),
      sensorData.temperature,
      sensorData.humidity,
      sensorData.pressure,
      sensorData.gas
    ]);


    return ContentService
      .createTextOutput(
        JSON.stringify({
          success: true
        })
      )
      .setMimeType(ContentService.MimeType.JSON);

  }


  catch (error) {

    return ContentService
      .createTextOutput(
        JSON.stringify({
          success: false,
          error: error.toString()
        })
      )
      .setMimeType(ContentService.MimeType.JSON);

  }
}


// --------------------------------
// SensorData 시트 가져오기
// 없으면 자동 생성
// --------------------------------

function getSensorSheet() {

  const props =
    PropertiesService.getScriptProperties();

  let spreadsheetId =
    props.getProperty("SPREADSHEET_ID");


  let spreadsheet;


  // 기존 Spreadsheet가 있으면 사용
  if (spreadsheetId) {

    spreadsheet =
      SpreadsheetApp.openById(spreadsheetId);

  }


  // 없으면 새 Spreadsheet 생성
  else {

    spreadsheet =
      SpreadsheetApp.create("Food Spoilage Monitor Data");

    spreadsheetId =
      spreadsheet.getId();

    props.setProperty(
      "SPREADSHEET_ID",
      spreadsheetId
    );
  }


  let sheet =
    spreadsheet.getSheetByName(SHEET_NAME);


  // SensorData 시트가 없으면 생성
  if (!sheet) {

    sheet =
      spreadsheet.insertSheet(SHEET_NAME);

    sheet.appendRow([
      "timestamp",
      "temperature",
      "humidity",
      "pressure",
      "gas_resistance"
    ]);

    sheet.setFrozenRows(1);
  }


  return sheet;
}


// --------------------------------
// 최신 센서값 반환
// --------------------------------

function getSensorData() {

  const saved =
    PropertiesService
      .getScriptProperties()
      .getProperty("SENSOR_DATA");


  console.log(
    "SENSOR_DATA =",
    saved
  );


  if (!saved) {

    return {
      temperature: 0,
      humidity: 0,
      pressure: 0,
      gas: 0,
      timestamp: "Waiting for ESP32..."
    };

  }


  return JSON.parse(saved);
}


// --------------------------------
// CSV 데이터 생성
// --------------------------------

function getCsvData() {

  const sheet =
    getSensorSheet();

  const values =
    sheet.getDataRange().getValues();


  return values
    .map(row =>
      row.map(value => {

        let text =
          String(value);

        // CSV 내부의 따옴표 처리
        text =
          text.replace(/"/g, '""');

        return `"${text}"`;

      }).join(",")
    )
    .join("\r\n");
}


// --------------------------------
// 웹페이지
// --------------------------------

function doGet(e) {

  // --------------------------------
  // CSV 다운로드 요청
  // --------------------------------

  if (
    e &&
    e.parameter &&
    e.parameter.download === "csv"
  ) {

    const csv =
      getCsvData();

    return ContentService
      .createTextOutput(
        "\uFEFF" + csv
      )
      .setMimeType(
        ContentService.MimeType.CSV
      );
  }


  // --------------------------------
  // 기존 웹페이지
  // --------------------------------

  return HtmlService
    .createHtmlOutput(`

<!DOCTYPE html>

<html>

<head>

<meta charset="UTF-8">

<meta name="viewport"
      content="width=device-width, initial-scale=1.0">

<title>Food Spoilage Monitor</title>

<style>

body {
  font-family: Arial, sans-serif;
  background: #f4f6f8;
  margin: 0;
  padding: 30px;
}

.container {
  max-width: 700px;
  margin: auto;
}

h1 {
  text-align: center;
}

.card {
  background: white;
  padding: 20px;
  margin-top: 15px;
  border-radius: 12px;
  box-shadow: 0 3px 10px rgba(0,0,0,0.08);
}

.sensor {
  display: flex;
  justify-content: space-between;
  padding: 14px 0;
  border-bottom: 1px solid #eee;
  font-size: 20px;
}

.sensor:last-child {
  border-bottom: none;
}

.value {
  font-weight: bold;
}

.status {
  text-align: center;
  font-size: 20px;
  padding: 15px;
}

.online {
  color: green;
}

.download {
  display: block;
  text-align: center;
  margin-top: 20px;
  padding: 14px;
  background: #333;
  color: white;
  text-decoration: none;
  border-radius: 8px;
}

</style>

</head>


<body>

<div class="container">

<h1>🍱 Food Spoilage Monitor</h1>


<div class="card">

<div class="sensor">
  <span>Temperature</span>
  <span class="value" id="temperature">-- °C</span>
</div>

<div class="sensor">
  <span>Humidity</span>
  <span class="value" id="humidity">-- %</span>
</div>

<div class="sensor">
  <span>Pressure</span>
  <span class="value" id="pressure">-- hPa</span>
</div>

<div class="sensor">
  <span>Gas Resistance</span>
  <span class="value" id="gas">-- kΩ</span>
</div>

</div>


<div class="card status">

  <div id="status">● WAITING FOR ESP32</div>

  <div id="time"></div>

</div>


<a
  class="download"
  href="?download=csv"
  target="_blank"
>
  📥 Download CSV
</a>


</div>


<script>

function updateSensor() {

  google.script.run

    .withSuccessHandler(function(data) {

      document.getElementById("temperature").textContent =
        data.temperature.toFixed(2) + " °C";

      document.getElementById("humidity").textContent =
        data.humidity.toFixed(2) + " %";

      document.getElementById("pressure").textContent =
        data.pressure.toFixed(2) + " hPa";

      document.getElementById("gas").textContent =
        data.gas.toFixed(2) + " kΩ";

      document.getElementById("time").textContent =
        "Last update: " + data.timestamp;

      document.getElementById("status").textContent =
        "● ESP32 ONLINE";

      document.getElementById("status").className =
        "online";

    })

    .withFailureHandler(function(error) {

      document.getElementById("status").textContent =
        "● CONNECTION ERROR";

      console.log(error);

    })

    .getSensorData();

}


// 2초마다 최신값 확인
setInterval(updateSensor, 2000);

updateSensor();

</script>


</body>

</html>

  `);
}