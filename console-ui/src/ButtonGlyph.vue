<script setup lang="ts">
// Controller button badge that follows the connected pad family (Xbox/Deck, PlayStation, Nintendo).
import { computed } from 'vue'
import { useGamepad } from './upstream/gamepad'
type Button = 'A' | 'B' | 'X' | 'Y' | 'LB' | 'RB' | 'Menu' | 'View'
const props = defineProps<{ button: Button; accent?: boolean; large?: boolean }>()
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
  <span class="glyph" :class="[`glyph-${family}`, `glyph-${button.toLowerCase()}`, { 'glyph-face': button.length === 1, 'glyph-accent': accent, 'glyph-large': large }]" aria-hidden="true">{{ text }}</span>
</template>

<style>
/* Round, neutral glyphs on every pad family; only the primary action's glyph carries the accent. */
.glyph { display: inline-grid; place-items: center; flex: none; width: 32px; height: 32px; border-radius: 50%; background: var(--pc-line, #34363a); color: var(--pc-text, #f1efea); font: 800 15px/1 'Archivo Variable', system-ui, sans-serif; vertical-align: middle; }
.glyph-lb, .glyph-rb { background: var(--pc-raised, #242528); color: var(--pc-text-2, #b4b1aa); font-weight: 700; }
.glyph-accent { background: var(--pc-accent, #62c554); color: var(--pc-on-accent, #0e1a0b); }
.glyph-large { width: 40px; height: 40px; font-size: 18px; }
</style>
