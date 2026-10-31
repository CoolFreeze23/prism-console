<script setup lang="ts">
// A pack's icon shown as its artwork, in a flat case tinted from the icon's own colours.
// Pixel art (64 px or smaller) is scaled by whole numbers with hard edges; larger pictures are fitted smoothly
// without cropping; a pack without an icon gets its initial on a tint derived from its name.
import { computed, ref, watch } from 'vue'
const props = defineProps<{ src?: string; name: string; size: number }>()
const natural = ref(0), tint = ref(''), broken = ref(false)
watch(() => props.src, () => { natural.value = 0; tint.value = ''; broken.value = false })
// Room inside the case: 1/8 of it on each side.
const inner = computed(() => Math.round(props.size * 0.75))
const pixelArt = computed(() => natural.value > 0 && natural.value <= 64)
const drawn = computed(() => {
  if (!natural.value) return inner.value
  if (!pixelArt.value) return inner.value
  return Math.max(natural.value, Math.floor(inner.value / natural.value) * natural.value)
})
function hashTint(text: string) {
  let hash = 0
  for (const c of text) hash = (hash * 31 + c.charCodeAt(0)) | 0
  return `hsl(${Math.abs(hash) % 360} 28% 18%)`
}
const caseColor = computed(() => tint.value || hashTint(props.name || '?'))
function loaded(event: Event) {
  const image = event.target as HTMLImageElement
  natural.value = Math.max(image.naturalWidth, image.naturalHeight)
  try {
    const canvas = document.createElement('canvas'); canvas.width = canvas.height = 8
    const context = canvas.getContext('2d', { willReadFrequently: true })
    if (!context) return
    context.drawImage(image, 0, 0, 8, 8)
    const data = context.getImageData(0, 0, 8, 8).data
    let r = 0, g = 0, b = 0, n = 0
    for (let i = 0; i < data.length; i += 4) if (data[i + 3] > 128) { r += data[i]; g += data[i + 1]; b += data[i + 2]; n++ }
    // The case is the icon's average colour, darkened to sit quietly behind it.
    if (n) tint.value = `rgb(${Math.round(r / n * 0.3)} ${Math.round(g / n * 0.3)} ${Math.round(b / n * 0.3)})`
  } catch { /* a tainted image keeps the name tint */ }
}
</script>

<template>
  <span class="pack-art" :style="{ width: size + 'px', height: size + 'px', background: caseColor }" aria-hidden="true">
    <img v-if="src && !broken" :src="src" alt="" :class="{ pixel: pixelArt }" :style="{ width: drawn + 'px', height: drawn + 'px' }" @load="loaded" @error="broken = true">
    <span v-else class="pack-initial" :style="{ fontSize: Math.round(size * 0.42) + 'px' }">{{ (name || '?').trim().charAt(0).toUpperCase() }}</span>
  </span>
</template>

<style>
.pack-art { display: inline-grid; place-items: center; flex: none; box-shadow: inset 0 0 0 1px #ffffff14; overflow: hidden; }
.pack-art img { object-fit: contain; image-rendering: auto; }
.pack-art img.pixel { image-rendering: pixelated; }
.pack-initial { font-weight: 700; font-stretch: 125%; color: #f1efeacc; line-height: 1; }
</style>
