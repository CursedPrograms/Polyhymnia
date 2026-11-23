// Polyhymnia GitHub Pages: feature cards and footer year.
(function () {
  'use strict';
  const features = [
    ['Edit window', 'SoundCraft's Pro Tools-style layout and icons: toolbar, tracks, rulers for bars, time, tempo, meter and markers, coloured clips with waveforms.'],
    ['Import', 'File > Import Audio or drag and drop WAV, FLAC, MP3, AIFF and OGG onto a track; the Clips list keeps them.'],
    ['Editing', 'Shuffle, Slip, Spot and Grid; cut, paste, duplicate, separate, heal, nudge; fades and crossfades in five shapes; undo.'],
    ['Mix window', 'Channel strips with inserts, sends, routing, pan, solo, mute, faders and meters; busses and the master.'],
    ['Effects and plugins', 'Built-in EQ, compressor, chorus, reverb and limiter, every setting a slider; VST3 plugins in the inserts, and the free Octavia plugins from the insert menu.'],
    ['Record and bounce', 'Arm tracks, monitor inputs, punch and loop recording; bounce to WAV, FLAC or MP3, stems too.'],
    ['Keyboard', 'Space plays and stops; shortcuts work anywhere except while you type in a field.'],
    ['On the way', 'ASIO, CLAP plugins, plugin editors in their own windows, automation, MIDI and video scoring.']
  ];
  const grid = document.getElementById('features');
  if (grid) {
    for (const [title, text] of features) {
      const card = document.createElement('article');
      const h = document.createElement('h2');
      h.textContent = title;
      const p = document.createElement('p');
      p.textContent = text;
      card.append(h, p);
      grid.appendChild(card);
    }
  }
  const year = document.getElementById('year');
  if (year) year.textContent = String(Math.max(2026, new Date().getFullYear()));
})();