<script setup lang="ts">
import { t } from './i18n'
import { computed, ref } from 'vue'
const props = defineProps<{ skin: string; slim?: boolean; cape?: string; elytra?: boolean }>()
const yaw = ref(-25), zoom = ref(5), layers = ref(true)
const faces = ['right', 'front', 'left', 'back', 'top', 'bottom']
const parts = computed(() => {
  const arm = props.slim ? 3 : 4
  const base = [
    { name: 'Head', x: -4, y: 0, w: 8, h: 8, d: 8, u: 0, v: 0, layer: false },
    { name: 'Body', x: -4, y: 8, w: 8, h: 12, d: 4, u: 16, v: 16, layer: false },
    { name: 'Right arm', x: -4-arm, y: 8, w: arm, h: 12, d: 4, u: 40, v: 16, layer: false },
    { name: 'Left arm', x: 4, y: 8, w: arm, h: 12, d: 4, u: 32, v: 48, layer: false },
    { name: 'Right leg', x: -4, y: 20, w: 4, h: 12, d: 4, u: 0, v: 16, layer: false },
    { name: 'Left leg', x: 0, y: 20, w: 4, h: 12, d: 4, u: 16, v: 48, layer: false },
  ]
  if (layers.value) base.push(...base.map((p, i) => ({ ...p, name: p.name + ' layer', u: [32,16,40,48,0,0][i], v: [0,32,32,48,32,48][i], layer: true })))
  return base
})
function faceStyle(p: typeof parts.value[number], face: string) {
  const s = zoom.value, expand = p.layer ? .35 : 0
  const w = p.w + expand, h = p.h + expand, d = p.d + expand
  const uv: Record<string, number[]> = { right: [p.u,p.v+p.d,p.d,p.h], front: [p.u+p.d,p.v+p.d,p.w,p.h], left: [p.u+p.d+p.w,p.v+p.d,p.d,p.h], back: [p.u+2*p.d+p.w,p.v+p.d,p.w,p.h], top: [p.u+p.d,p.v,p.w,p.d], bottom: [p.u+p.d+p.w,p.v,p.w,p.d] }
  const [u,v,fw,fh] = uv[face]
  const transforms: Record<string, string> = { front: `translateZ(${d*s/2}px)`, back: `translateZ(${-d*s/2}px) rotateY(180deg)`, right: `translateX(${-d*s/2}px) rotateY(-90deg)`, left: `translateX(${(w-d/2)*s}px) rotateY(90deg)`, top: `translateY(${-d*s/2}px) rotateX(90deg)`, bottom: `translateY(${(h-d/2)*s}px) rotateX(-90deg)` }
  return { width: fw*s+'px', height: fh*s+'px', transform: transforms[face], backgroundImage: `url("${props.skin}")`, backgroundSize: `${64*s}px ${64*s}px`, backgroundPosition: `${-u*s}px ${-v*s}px` }
}
</script>
<template>
  <div class="skin-preview">
    <div class="skin-stage" role="img" :aria-label="t('skin.minecraft_skin_preview')"><div class="skin-person" :style="{ transform: `rotateX(-10deg) rotateY(${yaw}deg)`, top: `calc(50% - ${16*zoom}px)` }"><div v-for="part in parts" :key="part.name" class="skin-part" :style="{ transform: `translate3d(${part.x*zoom}px,${part.y*zoom}px,0)`, width: part.w*zoom+'px', height: part.h*zoom+'px' }"><div v-for="face in faces" :key="face" class="skin-face" :style="faceStyle(part, face)" /></div><div v-if="cape && !elytra" class="skin-cape" :style="{ width: 10*zoom+'px', height: 16*zoom+'px', transform: `translate3d(${-5*zoom}px,${8*zoom}px,${-3*zoom}px) rotateX(10deg)`, backgroundImage: `url(${cape})`, backgroundSize: `${64*zoom}px ${32*zoom}px`, backgroundPosition: `${-zoom}px ${-zoom}px` }" /><template v-if="cape && elytra"><div v-for="side in [-1, 1]" :key="side" class="skin-cape" :style="{ width: 12*zoom+'px', height: 20*zoom+'px', transform: `translate3d(${(side < 0 ? -12 : 0)*zoom}px,${8*zoom}px,${-3*zoom}px) rotateZ(${side*18}deg) scaleX(${side})`, backgroundImage: `url(${cape})`, backgroundSize: `${64*zoom}px ${32*zoom}px`, backgroundPosition: `${-34*zoom}px ${-2*zoom}px` }" /></template></div></div>
    <div class="actions"><button @click="yaw -= 30">{{ t('skin.rotate_left') }}</button><button @click="yaw += 30">{{ t('skin.rotate_right') }}</button><button :disabled="zoom <= 3" @click="zoom--">{{ t('skin.zoom_out') }}</button><button :disabled="zoom >= 8" @click="zoom++">{{ t('skin.zoom_in') }}</button><button class="setting-toggle skin-layers" role="switch" :aria-checked="layers" @click="layers = !layers"><span class="setting-text"><span>{{ t('skin.outerLayers') }}</span></span><span class="switch" aria-hidden="true"><span /></span></button></div>
  </div>
</template>
<style scoped>
.skin-stage {height:300px;perspective:650px;overflow:hidden;background:radial-gradient(ellipse,#29472b,#111b13);border-radius:24px;position:relative}
.skin-person {position:absolute;left:50%;transform-style:preserve-3d;transition:transform .15s}
.skin-part,.skin-face,.skin-cape {position:absolute;transform-style:preserve-3d;image-rendering:pixelated;background-repeat:no-repeat}
.skin-face {backface-visibility:hidden}.skin-cape{backface-visibility:visible}
</style>
