// Interface sounds, synthesized with the Web Audio API: no audio files, so nothing to license. Short and quiet by
// design; silent whenever the page is hidden (the game has the screen) or the player turned sounds off.
import { reactive } from 'vue'

export type Sound = 'navigate' | 'confirm' | 'back' | 'error' | 'launch'
export const soundSettings = reactive({ enabled: true, volume: 35 })

type Note = { frequency: number; start: number; length: number; wave?: OscillatorType; level?: number }
const SOUNDS: Record<Sound, Note[]> = {
  navigate: [{ frequency: 1320, start: 0, length: 0.035, wave: 'triangle', level: 0.35 }],
  confirm: [{ frequency: 660, start: 0, length: 0.06 }, { frequency: 990, start: 0.055, length: 0.08 }],
  back: [{ frequency: 880, start: 0, length: 0.06 }, { frequency: 587, start: 0.055, length: 0.08 }],
  error: [{ frequency: 196, start: 0, length: 0.09, wave: 'triangle', level: 0.9 }, { frequency: 185, start: 0.1, length: 0.12, wave: 'triangle', level: 0.9 }],
  launch: [523, 659, 784].map((frequency, i) => ({ frequency, start: i * 0.07, length: 0.07 } as Note))
    .concat([{ frequency: 1047, start: 0.21, length: 0.22 }]),
}
const MASTER = 0.25  // even 100% stays well below the game's own level

let context: AudioContext | undefined
let lastNavigate = 0

export function play(sound: Sound) {
  if (!soundSettings.enabled || soundSettings.volume <= 0 || document.visibilityState === 'hidden') return
  if (sound === 'navigate') {
    if (performance.now() - lastNavigate < 70) return  // a held stick should not buzz
    lastNavigate = performance.now()
  }
  try {
    context ??= new AudioContext()
    if (context.state === 'suspended') void context.resume()
    const output = context.createGain()
    output.gain.value = MASTER * soundSettings.volume / 100
    output.connect(context.destination)
    for (const note of SOUNDS[sound]) {
      const start = context.currentTime + note.start
      const oscillator = context.createOscillator()
      const envelope = context.createGain()
      oscillator.type = note.wave ?? 'sine'
      oscillator.frequency.value = note.frequency
      // Soft attack and an exponential tail: no clicks.
      envelope.gain.setValueAtTime(0.0001, start)
      envelope.gain.exponentialRampToValueAtTime(note.level ?? 0.6, start + 0.008)
      envelope.gain.exponentialRampToValueAtTime(0.0001, start + note.length)
      oscillator.connect(envelope).connect(output)
      oscillator.start(start)
      oscillator.stop(start + note.length + 0.02)
    }
  } catch { /* no audio device: stay silent */ }
}
