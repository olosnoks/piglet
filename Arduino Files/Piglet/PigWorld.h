#pragma once
#include <Arduino.h>

// ================================================================
//  PigWorld — the PigletNode C3 scrolling-world animation, grown to
//  this 128x64 two-colour OLED. The blue area is the world (clouds,
//  birds, far hills, trees, rolling ground, butterfly or fireflies,
//  truffle hunts); the yellow band is the sky above it, with the sun
//  or the moon and stars by local solar time, and fireworks.
//
//  It is an extra *view*, not a new page: on the Pig page (4) and the
//  Mesh page (5) a single press first switches to the world view, and
//  the next press carries on to the next page as before. The page
//  logic (scanning, pause, mesh Node/Core) is untouched.
//
//  Driven by the existing state:
//    Pig page   -> wardriving: counter = 2.4G + 5G networks
//    Mesh Node  -> counter = networks found, searches until linked
//    Mesh Core  -> counter = records received, and every connected
//                  node trots along as its own pig (up to 6 on screen)
//  Each new network flies from the pig up into the counter. Every 100
//  networks (1000 records on a Core) the pig throws a fireworks party.
//  GPS speed turns the trot into a gallop (dust from 12 km/h, speed
//  lines from 50 km/h).
// ================================================================

// Call on a single button press, before cycling pages.
// Returns true if the press was used to switch the world view on.
bool pigWorldTakePress();

// True while the world view is showing (loop() should call pigWorldTick instead of the page renderer).
bool pigWorldActive();

// Draw the next frame. Call every loop() while pigWorldActive(); it paces itself to 20 fps.
void pigWorldTick();

// Fireworks and a happy hop (double press while the world view is showing).
void pigWorldCelebrate();
