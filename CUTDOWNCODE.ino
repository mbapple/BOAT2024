TaskHandle_t Task1;
#include <A4988.h>
#include <Smoothed.h>
#include <HardwareSerial.h>
#include <sbus.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
const char* host = "esp32";
const char* ssid = "Tevin";
const char* password = "password";
WebServer server(80);

/*
 * Login page
 */

const char* loginIndex =
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"//
        "</tr>"
        "<tr>"
             "<td>Username:</td>"
             "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";

/*
 * Server Index Page
 */

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";


#define RELAY_MIN 200
#define RELAY_MAX 1800
#define PWM_MIN 206
#define PWM_MAX 1800
bfs::SbusRx sbus_rx(&Serial2, 16, 17, true);
bfs::SbusData data;

// pin definitions
#define STEPPER_PUL 2
#define STEPPER_DIR 4
#define STEPPER_ENA 15
#define STEPPER_PWM 17
#define RUDDER_REFRESH 20
int CH7 = 0;
#define POWER_RELAY 26
#define VESC_PWM 27
//#define POWER_RELAY_PWM 1

// values for potentiometer
#define POTENTIOMETER_MIDDLE 253
#define POTENTIOMETER_ERROR 5

// VESC control with PWM
#define PWM_CHANNEL 0
#define PWM_FREQUENCY 100 // was 980?
#define PWM_RESOLUTION 8

// rudder control
// smooth input from rudder and the potentiometer value
Smoothed <int> rudderInput;
Smoothed <int> actualAngle;
A4988 stepper(200, STEPPER_DIR, STEPPER_PUL);
  int msDelay = 500;

void setup(void) {
    xTaskCreatePinnedToCore(
                    Task1code,   /* Task function. */
                    "Task1",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task1,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  delay(500); 
  Serial.begin(115200);

  sbus_rx.Begin();
  
  pinMode(POWER_RELAY, OUTPUT);

  /** set up VESC output **/
  pinMode(VESC_PWM, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(VESC_PWM, PWM_CHANNEL);
  
  /** setup stepper motor **/
  pinMode(STEPPER_PUL, OUTPUT);
  pinMode(STEPPER_DIR, OUTPUT);
  pinMode(STEPPER_ENA, OUTPUT);
  pinMode(STEPPER_PWM, INPUT);
  rudderInput.begin(SMOOTHED_AVERAGE, 5);
  actualAngle.begin(SMOOTHED_AVERAGE, 15);
  stepper.begin(120, 1);
}

void handleRudder()
{
  int stepper_input = map(data.ch[0], PWM_MIN, PWM_MAX, 135, 225);
  rudderInput.add(stepper_input);
  int smoothed_stepper_input = rudderInput.get();
  Serial.print("Stepper Input: "); Serial.println(smoothed_stepper_input);
  int actual_angle_measurment = map(analogRead(34), 0, 4095, 0, 360) + (180 - POTENTIOMETER_MIDDLE);
  //Serial.print("Read Value: "); Serial.println(analogRead(34));
  actualAngle.add(actual_angle_measurment);
  int smoothed_actual_angle = actualAngle.get();
  Serial.print("Actual Angle: "); Serial.println(actual_angle_measurment);
  //delay(50);

  /*
  Possible angles: 135-225**
  */


  int current_error = smoothed_actual_angle - smoothed_stepper_input;
  if(abs(current_error < 5)) { // if close to correct angle
    // if going straight
    if(smoothed_stepper_input >=  175 && smoothed_stepper_input <= 185) {
      if(abs(smoothed_actual_angle - 180) >= POTENTIOMETER_ERROR)  { // allow for error
        if(smoothed_actual_angle > 180) { // too far right
          stepper.move(-1); 
        }
        else { // too far left
          stepper.move(1);
        }
      }
    
    } else if(abs(smoothed_stepper_input - smoothed_actual_angle) >= POTENTIOMETER_ERROR) { // if not going straight, allow for two degree erro
      if(smoothed_actual_angle > smoothed_stepper_input) { // too far right
        stepper.move(-1); // move left
      }
      else { // too far right
        stepper.move(1); // move left
      }
    }
  }
  else { // if need to make big correction
    // if going straight
    if(smoothed_stepper_input >=  175 && smoothed_stepper_input <= 185) {
      if(abs(actual_angle_measurment - 180) >= POTENTIOMETER_ERROR)  { // allow for error
        if(actual_angle_measurment > 180) { // too far right
          stepper.move(-1); 
        }
        else { // too far left
          stepper.move(1);
        }
      }
    
    } else if(abs(smoothed_stepper_input - smoothed_actual_angle) >= 2) { // if not going straight, allow for two degree error
        if(actual_angle_measurment > smoothed_stepper_input) { // too far right
          stepper.move(-1); // move left
        }
        else { // too far right
          stepper.move(1); // move left
        }
    }
    
  }
  //delay(100);
}

void loop(void) {
    if (sbus_rx.Read()) {
    /* Grab the received data */
    data = sbus_rx.data();
    } // retrieve data from receiver

  //Serial.println(data.ch[2]);
  int relay_output = map(data.ch[6], RELAY_MIN, RELAY_MAX, 0,1);
  int VESC_output = map(data.ch[2], 150, 1850, 0, 255);

  //Serial.println(data.ch[2]);
  //Serial.println(VESC_output);

  ledcWrite(0, VESC_output);
  if(relay_output > 0)
  {
    digitalWrite(POWER_RELAY, HIGH);
  }
  else{
    digitalWrite(POWER_RELAY, LOW);
  }

  handleRudder();
  

}

void Task1code( void * pvParameters ){

  for(;;){
      
  server.handleClient();
  delay(1);
  }
}
