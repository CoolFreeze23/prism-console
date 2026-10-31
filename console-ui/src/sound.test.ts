// @vitest-environment happy-dom
import { afterEach, describe, expect, it, vi } from 'vitest'
import { play, soundSettings } from './sound'
// A minimal Web Audio stand-in that counts the notes started.
const started: number[] = []
class FakeParam { value = 0; setValueAtTime() {} exponentialRampToValueAtTime() {} }
class FakeNode { gain = new FakeParam(); frequency = new FakeParam(); type = ''; connect<T>(next: T) { return next } start() { started.push(1) } stop() {} }
class FakeContext { state = 'running'; currentTime = 0; destination = {}; createGain() { return new FakeNode() } createOscillator() { return new FakeNode() } resume() {} }
vi.stubGlobal('AudioContext', FakeContext)
let hidden = false
Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => hidden ? 'hidden' : 'visible' })
afterEach(() => { started.length = 0; hidden = false; Object.assign(soundSettings, { enabled: true, volume: 35 }) })
describe('interface sounds', () => {
  it('plays short synthesized notes', () => {
    play('confirm')
    expect(started).toHaveLength(2)
  })
  it('stays silent when turned off, at zero volume, or while the game has the screen', () => {
    soundSettings.enabled = false; play('confirm')
    soundSettings.enabled = true; soundSettings.volume = 0; play('confirm')
    soundSettings.volume = 35; hidden = true; play('launch')
    expect(started).toHaveLength(0)
  })
  it('does not buzz while focus moves quickly', () => {
    play('navigate'); play('navigate'); play('navigate')
    expect(started).toHaveLength(1)
  })
})
