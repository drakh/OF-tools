#pragma once

#include "ofMain.h"
#include "Control.h"




class ofApp : public ofBaseApp
{
public:
    ~ofApp();
    void setup();
    void update();
    void draw();
    
    void toggleNotifier(GuiButtonEventArgs & s) {
        cout << "toggle notifier ! " << s.name << " : " << s.value << endl;
    }
    
    void buttonNotifier(GuiButtonEventArgs & s) {
        cout << "button notifier ! " << s.name << " : " << s.value << endl;
    }
    
    void menuNotifier(GuiButtonEventArgs & s) {
        cout << "menu select:: "<<s.name<< " : " << s.value << endl;
    }

    void menuNotifierMultiChoice(GuiButtonEventArgs & s) {
        cout << "menu (multiple choice) select:: "<< s.name << " : " << s.value << endl;
    }
    
    GuiButton *button;
    GuiToggle *toggle;
    GuiToggle *toggleNotify;
    
    GuiSlider<float> *floatSlider;
    GuiSlider<int> *intSlider;
    GuiSlider<double> *doubleSlider;
    
    GuiMenu *menu, *menuMultiChoice;
    GuiColor *colorPicker;
    
    bool myToggle;
    float floatValue;
    int intValue;
    double doubleValue;
    ofFloatColor bgColor;
};
