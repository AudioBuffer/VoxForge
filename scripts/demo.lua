-- 黑暗系（你偏好的那版）
synth.voice("female")
synth.set{ tilt=8, open=0.40, human=1.0, air=0 }
synth.phrase{ text="ni:220 hao:247", dur=450, glide=50 }
synth.phrase{ text="wo:220 ai:262 ni:294", dur=420, glide=45 }
synth.save("/sdcard/Download/DSH-Go/lua_demo.wav")
