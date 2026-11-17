// Polyhymnia GitHub Pages: feature cards and footer year.
(function () {
  'use strict';
  const features = [
    ['Edit window', 'Bars/beats and minutes:seconds rulers, track headers, clip blocks with peak waveforms, playhead and markers.'],
    ['Session', 'Tracks, clips, sources, markers, tempo and meter in a text `.polyhymnia` project beside the audio files.'],
    ['Playback', 'Transport bar, loop, mix with gain, pan, mute and solo on the audio thread.'],
    ['Real audio', 'WAV, FLAC and MP3 through miniaudio; offline `--render` and a 1 kHz tone `--audiotest` hook.'],
    ['Atelier look', 'Greys and Atelier blue `#007bb7`, 12 languages, splash screen.'],
    ['On the way', 'Editing tools, Mix window, recording, MIDI, bounce, plugins and video scoring.']
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