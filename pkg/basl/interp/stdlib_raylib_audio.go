package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func (interp *Interpreter) rlAudio(env *Env) {
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	// ── Audio device ──
	nf("init_audio_device", func(a []value.Value) (value.Value, error) { rl.InitAudioDevice(); return value.Void, nil })
	nf("close_audio_device", func(a []value.Value) (value.Value, error) { rl.CloseAudioDevice(); return value.Void, nil })
	nf("is_audio_device_ready", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsAudioDeviceReady()), nil })
	nf("set_master_volume", func(a []value.Value) (value.Value, error) { rl.SetMasterVolume(f32(a[0])); return value.Void, nil })
	nf("get_master_volume", func(a []value.Value) (value.Value, error) { return value.NewF64(float64(rl.GetMasterVolume())), nil })

	// ── Wave ──
	nf("load_wave", func(a []value.Value) (value.Value, error) {
		w := rl.LoadWave(a[0].AsString())
		if w.FrameCount == 0 {
			return value.Void, fmt.Errorf("rl.load_wave: failed to load %q", a[0].AsString())
		}
		return valNative("Wave", &w), nil
	})
	nf("unload_wave", func(a []value.Value) (value.Value, error) {
		rl.UnloadWave(*argNative[rl.Wave](a[0]))
		return value.Void, nil
	})
	nf("export_wave", func(a []value.Value) (value.Value, error) {
		rl.ExportWave(*argNative[rl.Wave](a[0]), a[1].AsString())
		return value.Void, nil
	})

	// ── Sound ──
	nf("load_sound", func(a []value.Value) (value.Value, error) {
		s := rl.LoadSound(a[0].AsString())
		if s.Stream.SampleRate == 0 {
			return value.Void, fmt.Errorf("rl.load_sound: failed to load %q", a[0].AsString())
		}
		return valNative("Sound", &s), nil
	})
	nf("load_sound_from_wave", func(a []value.Value) (value.Value, error) {
		s := rl.LoadSoundFromWave(*argNative[rl.Wave](a[0]))
		return valNative("Sound", &s), nil
	})
	nf("unload_sound", func(a []value.Value) (value.Value, error) {
		rl.UnloadSound(*argNative[rl.Sound](a[0]))
		return value.Void, nil
	})
	nf("play_sound", func(a []value.Value) (value.Value, error) {
		rl.PlaySound(*argNative[rl.Sound](a[0]))
		return value.Void, nil
	})
	nf("stop_sound", func(a []value.Value) (value.Value, error) {
		rl.StopSound(*argNative[rl.Sound](a[0]))
		return value.Void, nil
	})
	nf("pause_sound", func(a []value.Value) (value.Value, error) {
		rl.PauseSound(*argNative[rl.Sound](a[0]))
		return value.Void, nil
	})
	nf("resume_sound", func(a []value.Value) (value.Value, error) {
		rl.ResumeSound(*argNative[rl.Sound](a[0]))
		return value.Void, nil
	})
	nf("is_sound_playing", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsSoundPlaying(*argNative[rl.Sound](a[0]))), nil
	})
	nf("set_sound_volume", func(a []value.Value) (value.Value, error) {
		rl.SetSoundVolume(*argNative[rl.Sound](a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("set_sound_pitch", func(a []value.Value) (value.Value, error) {
		rl.SetSoundPitch(*argNative[rl.Sound](a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("set_sound_pan", func(a []value.Value) (value.Value, error) {
		rl.SetSoundPan(*argNative[rl.Sound](a[0]), f32(a[1]))
		return value.Void, nil
	})

	// ── Music ──
	nf("load_music_stream", func(a []value.Value) (value.Value, error) {
		m := rl.LoadMusicStream(a[0].AsString())
		if m.Stream.SampleRate == 0 {
			return value.Void, fmt.Errorf("rl.load_music_stream: failed to load %q", a[0].AsString())
		}
		return valNative("Music", &m), nil
	})
	nf("unload_music_stream", func(a []value.Value) (value.Value, error) {
		rl.UnloadMusicStream(*argNative[rl.Music](a[0]))
		return value.Void, nil
	})
	nf("play_music_stream", func(a []value.Value) (value.Value, error) {
		rl.PlayMusicStream(*argNative[rl.Music](a[0]))
		return value.Void, nil
	})
	nf("update_music_stream", func(a []value.Value) (value.Value, error) {
		rl.UpdateMusicStream(*argNative[rl.Music](a[0]))
		return value.Void, nil
	})
	nf("stop_music_stream", func(a []value.Value) (value.Value, error) {
		rl.StopMusicStream(*argNative[rl.Music](a[0]))
		return value.Void, nil
	})
	nf("pause_music_stream", func(a []value.Value) (value.Value, error) {
		rl.PauseMusicStream(*argNative[rl.Music](a[0]))
		return value.Void, nil
	})
	nf("resume_music_stream", func(a []value.Value) (value.Value, error) {
		rl.ResumeMusicStream(*argNative[rl.Music](a[0]))
		return value.Void, nil
	})
	nf("is_music_stream_playing", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsMusicStreamPlaying(*argNative[rl.Music](a[0]))), nil
	})
	nf("set_music_volume", func(a []value.Value) (value.Value, error) {
		rl.SetMusicVolume(*argNative[rl.Music](a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("set_music_pitch", func(a []value.Value) (value.Value, error) {
		rl.SetMusicPitch(*argNative[rl.Music](a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("set_music_pan", func(a []value.Value) (value.Value, error) {
		rl.SetMusicPan(*argNative[rl.Music](a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("get_music_time_length", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(rl.GetMusicTimeLength(*argNative[rl.Music](a[0])))), nil
	})
	nf("get_music_time_played", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(rl.GetMusicTimePlayed(*argNative[rl.Music](a[0])))), nil
	})
	nf("seek_music_stream", func(a []value.Value) (value.Value, error) {
		rl.SeekMusicStream(*argNative[rl.Music](a[0]), f32(a[1]))
		return value.Void, nil
	})
}
