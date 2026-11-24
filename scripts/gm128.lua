-- GM128 音色插件（原型）
-- 128 个 GM 音色映射到 12 种合成原型 + 参数微调
-- 用法: kt_lua scripts/gm128.lua

local GM = {
  -- 0-7 钢琴
  [0]="Piano", [1]="BrightPiano", [2]="ElecGrand", [3]="HonkyTonk",
  [4]="ElecPiano1", [5]="ElecPiano2", [6]="Harpsichord", [7]="Clavinet",
  -- 8-15 色彩打击
  [8]="Celesta", [9]="Glockenspiel", [10]="MusicBox", [11]="Vibraphone",
  [12]="Marimba", [13]="Xylophone", [14]="TubularBell", [15]="Dulcimer",
  -- 16-23 风琴
  [16]="DrawbarOrgan", [17]="PercOrgan", [18]="RockOrgan", [19]="ChurchOrgan",
  [20]="ReedOrgan", [21]="Accordion", [22]="Harmonica", [23]="TangoAccordion",
  -- 24-31 吉他
  [24]="NylonGuitar", [25]="SteelGuitar", [26]="JazzGuitar", [27]="CleanGuitar",
  [28]="MutedGuitar", [29]="OverdriveGuitar", [30]="DistortionGuitar", [31]="GuitarHarmonics",
  -- 32-39 贝斯
  [32]="AcousticBass", [33]="FingerBass", [34]="PickBass", [35]="FretlessBass",
  [36]="SlapBass1", [37]="SlapBass2", [38]="SynthBass1", [39]="SynthBass2",
}

-- 12 种原型参数：attack/decay/sustain/release/bright/noise
local ARCH = {
  piano    = {a=0.002, d=0.6,  s=0.25, r=0.25, br=0.55, nz=0.01},
  bell     = {a=0.001, d=1.2,  s=0.05, r=0.6,  br=0.85, nz=0.00},
  organ    = {a=0.02,  d=0.05, s=0.90, r=0.15, br=0.45, nz=0.00},
  guitar   = {a=0.003, d=0.5,  s=0.20, r=0.2,  br=0.65, nz=0.01},
  bass     = {a=0.005, d=0.4,  s=0.55, r=0.15, br=0.30, nz=0.00},
  strings  = {a=0.12,  d=0.2,  s=0.85, r=0.35, br=0.50, nz=0.02},
  brass    = {a=0.05,  d=0.1,  s=0.80, r=0.2,  br=0.70, nz=0.02},
  reed     = {a=0.04,  d=0.1,  s=0.85, r=0.15, br=0.40, nz=0.03},
  flute    = {a=0.06,  d=0.1,  s=0.80, r=0.2,  br=0.20, nz=0.05},
  lead     = {a=0.01,  d=0.2,  s=0.70, r=0.2,  br=0.75, nz=0.01},
  pad      = {a=0.3,   d=0.4,  s=0.90, r=0.8,  br=0.35, nz=0.03},
  perc     = {a=0.001, d=0.12, s=0.0,  r=0.08, br=0.9,  nz=0.4},
}

-- 音色名 -> 原型
local function archOf(program)
  local name = GM[program] or "pad"
  if name:find("Piano") or name:find("Harpsichord") or name:find("Clavinet") then return "piano" end
  if name:find("Bell") or name:find("Glocken") or name:find("Celesta")
     or name:find("Vibra") or name:find("Marimba") or name:find("Xylo")
     or name:find("Dulcimer") then return "bell" end
  if name:find("Organ") or name:find("Accordion") or name:find("Harmonica") then return "organ" end
  if name:find("Guitar") then return "guitar" end
  if name:find("Bass") then return "bass" end
  return "pad"
end

local function note(program, hz, dur)
  local name = GM[program] or "pad"
  local arch = ARCH[archOf(program)]
  synth.tone{
    program = program, hz = hz, dur = dur,
    attack = arch.a, decay = arch.d, sustain = arch.s, release = arch.r,
    bright = arch.br, noise = arch.nz,
  }
end

-- 演示：几组不同类别的音色
note(0,  261.63, 0.5)   -- 钢琴 C4
note(11, 659.25, 0.8)   -- 电颤琴 E5
note(19, 261.63, 0.9)   -- 教堂管风琴
note(25, 392.00, 0.6)   -- 民谣吉他 G4
note(32, 82.41,  0.7)   -- 原声贝斯 E2
note(0,  523.25, 0.4)   -- 钢琴 C5 收尾

synth.save("/sdcard/Download/DSH-Go/gm_demo.wav")
