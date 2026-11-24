local sf = "/storage/emulated/0/ 我的文件/TimGM6mb.sf2"
-- 伴奏轨：钢琴 + 贝斯
synth.instrument{ sf=sf, program=0, notes="C4:0.5 E4:0.5 G4:0.5 C5:0.8" }
synth.instrument{ sf=sf, program=32, notes="C2:0.5 G2:0.5 C2:0.8" }
-- 歌声轨
synth.voice("female")
synth.set{ tilt=8, open=0.40, auto=true }
synth.phrase{ text="ni:262 hao:294 shi:330 jie:349", dur=400 }
synth.save("/sdcard/Download/DSH-Go/band.wav")
