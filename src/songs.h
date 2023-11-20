#include "pitches.h"
#define BUZZER_PIN 4

// for music theory nerds: you can set duration of 500 to be a quarter note
void song1()
{
    // nokia ringtone lol
    tone(BUZZER_PIN, NOTE_E5, 125);
    tone(BUZZER_PIN, NOTE_D5, 125);
    tone(BUZZER_PIN, NOTE_FS4, 250);
    tone(BUZZER_PIN, NOTE_GS4, 250);
    tone(BUZZER_PIN, NOTE_CS5, 125);
    tone(BUZZER_PIN, NOTE_B4, 125);
    tone(BUZZER_PIN, NOTE_D4, 250);
    tone(BUZZER_PIN, NOTE_E4, 250);
    tone(BUZZER_PIN, NOTE_B4, 125);
    tone(BUZZER_PIN, NOTE_A4, 125);
    tone(BUZZER_PIN, NOTE_CS4, 250);
    tone(BUZZER_PIN, NOTE_E4, 250);
    tone(BUZZER_PIN, NOTE_A4, 500);
}

void song2()
{
    // mii channel theme
    tone(BUZZER_PIN, NOTE_FS4, 500);
    tone(BUZZER_PIN, NOTE_A4, 250);
    tone(BUZZER_PIN, NOTE_CS5, 500);
    tone(BUZZER_PIN, NOTE_A4, 500);
    tone(BUZZER_PIN, NOTE_FS4, 250);
    tone(BUZZER_PIN, NOTE_D4, 250);
    tone(BUZZER_PIN, NOTE_D4, 250);
    tone(BUZZER_PIN, NOTE_D4, 500);
    tone(BUZZER_PIN, REST, 750);
    tone(BUZZER_PIN, NOTE_CS4, 250);

    tone(BUZZER_PIN, NOTE_D4, 250);
    tone(BUZZER_PIN, NOTE_FS4, 250);
    tone(BUZZER_PIN, NOTE_A4, 250);
    tone(BUZZER_PIN, NOTE_CS5, 500);
    tone(BUZZER_PIN, NOTE_A4, 500);
    tone(BUZZER_PIN, NOTE_FS4, 250);

    tone(BUZZER_PIN, NOTE_E5, 750);
    tone(BUZZER_PIN, NOTE_DS5, 250);
    tone(BUZZER_PIN, NOTE_D5, 500);
}

void song3()
{
    // rickrolled!!!
    tone(BUZZER_PIN, NOTE_C4, 125);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_F4, 125);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_A4, 375);
    tone(BUZZER_PIN, NOTE_A4, 375);
    tone(BUZZER_PIN, NOTE_G4, 750);

    tone(BUZZER_PIN, NOTE_C4, 125);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_F4, 125);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_G4, 375);
    tone(BUZZER_PIN, NOTE_G4, 375);
    tone(BUZZER_PIN, NOTE_F4, 750);

    tone(BUZZER_PIN, NOTE_C4, 125);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_F4, 125);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_F4, 500);
    tone(BUZZER_PIN, NOTE_G4, 250);
    tone(BUZZER_PIN, NOTE_E4, 375);
    tone(BUZZER_PIN, NOTE_D4, 125);
    tone(BUZZER_PIN, NOTE_C4, 500);

    tone(BUZZER_PIN, NOTE_C4, 250);
    tone(BUZZER_PIN, NOTE_G4, 500);
    tone(BUZZER_PIN, NOTE_F4, 1000);
}