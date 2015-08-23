// This is a demonstration on how to use an input device to trigger changes on your neo pixels.
// You should wire a momentary push button to connect from ground to a digital IO pin.  When you
// press the button it will change to a new pixel animation.  Note that you need to press the
// button once to start the first animation!

#include <Adafruit_NeoPixel.h>

#define BUTTON_PIN   3    // Digital IO pin connected to the button.  This will be
                          // driven with a pull-up resistor so the switch should
                          // pull the pin to ground momentarily.  On a high -> low
                          // transition the button press logic will execute.

#define PIXEL_PIN    6    // Digital IO pin connected to the NeoPixels.

#define PIXEL_COUNT 300

// Read Brightness POT
int brightPin = 2;
int brightVal = 0;
int brightness = 0;
int oldBright = 0;


int wait = 5;
int rainbow_wait = 20;

// Allow button presses while in rainbow
int lightCycle = 0;

long previousMillis; // will store last time pixel was updated
int neoPixelToChange = 0; //track which neoPixel to change
int neoPixel_j = 0; //stores values for program cycles

// Parameter 1 = number of pixels in strip,  neopixel stick has 8
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_RGB     Pixels are wired for RGB bitstream
//   NEO_GRB     Pixels are wired for GRB bitstream, correct for neopixel stick
//   NEO_KHZ400  400 KHz bitstream (e.g. FLORA pixels)
//   NEO_KHZ800  800 KHz bitstream (e.g. High Density LED strip), correct for neopixel stick
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

bool oldState = HIGH;
int showType = 0;

void setup() {
 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
}

void loop() {
  // Get current button state.
  bool newState = digitalRead(BUTTON_PIN);

  // Check if state changed from high to low (button press).
  if (newState == LOW && oldState == HIGH) {
    // Short delay to debounce button.
    delay(20);
    // Check if button is still low after debounce.
    newState = digitalRead(BUTTON_PIN);
    if (newState == LOW) {
      showType++;
      if (showType > 9)
        showType=0;
      startShow(showType);
    }
  } else {
    startShow(showType);
  }

  // Set the last button state to the old state.
  oldState = newState;

  brightVal  = analogRead(brightPin); 

  // if brightVal has changed by +/- 10 then update (else stay dim)
  if (brightVal >= (oldBright+10) || brightVal <= (oldBright-10)) { 
    brightness = map(brightVal, 0, 1023, 0, 255);
    strip.setBrightness(brightness);
    oldBright = brightVal;
  }
}



void startShow(int i) {
  switch(i){
    case 0: colorWipe(strip.Color(0, 0, 0), wait);    // Black/off
            break;
    case 1: colorWipe(strip.Color(255, 0, 0), wait);  // Red
            break;
    case 2: colorWipe(strip.Color(0, 255, 0), wait);  // Green
            break;
    case 3: colorWipe(strip.Color(0, 0, 255), wait);  // Blue
            break;
    case 4: theaterChase(strip.Color(127, 127, 127), wait); // White
            break;
    case 5: theaterChase(strip.Color(127,   0,   0), wait); // Red
            break;
    case 6: theaterChase(strip.Color(  0,   0, 127), wait); // Blue
            break;
    case 7: rainbow(rainbow_wait);
            break;
    case 8: rainbowCycle(rainbow_wait);
            break;
    case 9: theaterChaseRainbow(wait);
            break;
  }
}

// Fill the dots one after the other with a color
//void colorWipe(uint32_t c, uint8_t wait) {
//  for(uint16_t i=0; i<strip.numPixels(); i++) {
//    strip.setPixelColor(i, c);
//    strip.show();
//    delay(wait);
//  }
//}

void colorWipe(uint32_t c, uint8_t wait) {

  unsigned long currentMillis = millis();
  
  //only do this if some of the pixels still need to be lit
  if (neoPixelToChange <= strip.numPixels()){
    
    if(currentMillis - previousMillis > wait * neoPixelToChange) { //appears to be an exponential growth delay but works
      
      // save the last time you changed a NeoPixel 
      previousMillis = currentMillis;  
    
      //change a pixel
      strip.setPixelColor(neoPixelToChange, c);
      strip.show();
      neoPixelToChange++;
    }
  }
}

void rainbow(uint8_t wait) {
  uint16_t i;
  lightCycle++;
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i+lightCycle) & 255));
    }
    strip.show();
    delay(wait);
 
  if(lightCycle == 256){
    lightCycle = 0;
    }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
  uint16_t i;
  lightCycle++;

    for(i=0; i< strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + lightCycle) & 255));
    }
    strip.show();
    delay(wait);
    
   if(lightCycle == 256){
    lightCycle = 0;
    }
}

//Theatre-style crawling lights.
void theaterChase(uint32_t c, uint8_t wait) {
  for (int j=0; j<2; j++) {  //do 1 cycles of chasing

    for (int q=0; q < 3; q++) {
      for (int i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, c);    //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (int i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  }
}

//Theatre-style crawling lights with rainbow effect
void theaterChaseRainbow(uint8_t wait) {
  // for (int j=0; j < 256; j++) {     // cycle all 256 colors in the wheel
 
    lightCycle++;
    for (int q=0; q < 3; q++) {
      for (int i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, Wheel( (i+lightCycle) % 255));    //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (int i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  //}
  if(lightCycle == 256){
    lightCycle = 0;}
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
