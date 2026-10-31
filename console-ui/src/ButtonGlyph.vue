<script setup lang="ts">
// Controller button badge that follows the connected pad family (Xbox/Deck, PlayStation, Nintendo).
import { computed } from 'vue'
import { useGamepad } from './upstream/gamepad'
type Button = 'A' | 'B' | 'X' | 'Y' | 'LB' | 'RB' | 'Menu' | 'View'
const props = defineProps<{ button: Button }>()
const gamepad = useGamepad()
const face: Partial<Record<Button, 'confirm' | 'cancel' | 'backspace' | 'keyboard'>> = { A: 'confirm', B: 'cancel', X: 'backspace', Y: 'keyboard' }
const text = computed(() => {
  const labels = gamepad.labels?.value
  const key = face[props.button]
  if (key) return labels?.[key] ?? props.button
  const [left, right] = (labels?.bumpers ?? 'LB/RB').split('/')
  if (props.button === 'LB') return left
  if (props.button === 'RB') return right
  return props.button === 'Menu' ? '☰' : '⧉'
})
const family = computed(() => gamepad.isPlayStation?.value ? 'ps' : gamepad.type?.value === 'switch' || gamepad.type?.value === 'wii' ? 'nintendo' : 'xbox')
</script>

<template>
  <span class="glyph" :class="[`glyph-${family}`, `glyph-${button.toLowerCase()}`, { 'glyph-face': button.length === 1 }]" aria-hidden="true">{{ text }}</span>
</template>

<style>
.glyph { display: inline-grid; place-items: center; min-width: 30px; height: 30px; padding: 0 8px; border-radius: 8px; background: #141515; border: 2px solid #5c5d5f; color: #fff; font: 700 13px/1 'Inter Variable', system-ui, sans-serif; letter-spacing: .02em; vertical-align: middle; }
.glyph-face { width: 30px; padding: 0; border-radius: 50%; font-size: 15px; }
.glyph-xbox.glyph-a { background: #1f7a2b; border-color: #5fd16c; }
.glyph-xbox.glyph-b { background: #8f2424; border-color: #ff6b6b; }
.glyph-xbox.glyph-x { background: #1c4f96; border-color: #5aa0ff; }
.glyph-xbox.glyph-y { background: #8a6d10; border-color: #ffd23f; }
.glyph-ps.glyph-face { background: #141515; }
.glyph-ps.glyph-a { color: #7fb2ff; border-color: #7fb2ff; }
.glyph-ps.glyph-b { color: #ff7a7a; border-color: #ff7a7a; }
.glyph-ps.glyph-x { color: #f29be0; border-color: #f29be0; }
.glyph-ps.glyph-y { color: #5fd1a6; border-color: #5fd1a6; }
</style>
