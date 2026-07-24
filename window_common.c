#include "au_window.h"
#include "au_containers.h"

#include <stdio.h>
#include <string.h>

FILE *g_input_recording_file = NULL;

typedef struct optional_u64 { u64 val; bool some; } Optional_U64;

static Optional_U64 serialize_bools(const bool *bools, u16 n_bools, u8 *out, u64 out_size)
{
	u16 n_out_bytes = n_bools / 8 + (u16) (n_bools % 8 != 0);
	if (out_size < sizeof(u16) + n_out_bytes) {
		return (Optional_U64) { 0 };
	} else {
		*((u16 *) out) = n_bools;
		out += sizeof(u16);
		for (u64 i=0; i<n_out_bytes; i++) {
			const bool *bools_i = &bools[i*8];
			u8 val = 0;
			for (i32 j=0; j<8; j++) {
				val |= bools_i[j] << j;
			}
			*out++ = val;
		}
		return (Optional_U64) { sizeof(u16) + (u64) n_out_bytes, true };
	}
}

static Optional_U64 deserialize_bools(const u8 *buf, bool *out)
{
	u16 n_bools = * (u16 *) buf;
	buf += sizeof(u16);
	u16 remaining_bits = n_bools & 0x7;
	u16 n_bytes = n_bools / 8 + (u16) (remaining_bits != 0);
	for (i32 i=0; i<n_bytes; i++) {
		u16 n_bits = i == n_bytes - 1 ? remaining_bits : 8;
		u8 byte = buf[i];
		for (i32 j=0; j<n_bits; j++) {
			*out++ = byte & (1 << j);
		}
	}
	return (Optional_U64) { sizeof(u16) + n_bytes, true };
}

static Optional_U64 serialize_char_dynarray(Char_Dynarray *arr, u8 *out, u64 out_size)
{
	if (out_size <  sizeof(u64) + arr->length) {
		return (Optional_U64) { 0 };
	} else {
		*((u64 *) out) = arr->length;
		out += sizeof(u64);
		memcpy(out, arr->d, arr->length * sizeof(u64));
		return (Optional_U64) { sizeof(u64) + arr->length, true };
	}
}

static Optional_U64 deserialize_char_dynarray(const u8 *buf, Char_Dynarray *arr)
{
	arr->length = 0;
	u64 n_chars = * (u64 *) buf;
	buf += sizeof(u64);
	for (u64 i=0; i<n_chars; i++) {
		char val = * (char *) buf++;
		dynarray_add(arr, &val);
	}
	return (Optional_U64) { sizeof(u64) + n_chars, true };
}

u64 input_state_max_serial_size(const Input_State *state)
{
	u64 ret = 1 // flags
		+ 3 * (sizeof(u16) + AU_MOUSE_BUTTON_COUNT / 8 + 1) // mouse arrays
		+ sizeof(i32)*4 + sizeof(float)*2
		+ 3 * (sizeof(u16) + AU_KEY_COUNT / 8 + 1) // key arrays
		+ sizeof(u64) + state->text_entered->length
		+ sizeof(u64)*5 + sizeof(double)*2;
	return ret;
}

Optional_U64 input_state_serialize(const Input_State *input, u8 *buffer, u64 buffer_size)
{
	Optional_U64 res = { 0 };
	u8 *p = buffer;
	i64 remain = buffer_size;
	if (remain >= 1) {
		u8 *flags = (u8 *) p++;
		remain--;
		if (input->quit) *flags |= 0x1;
		if (input->focus_gained) *flags |= 0x2;
		if (input->focus_lost) *flags |= 0x4;
		if (input->window_resized) *flags |= 0x8;
		if (input->window_rescaled) *flags |= 0x10;
	} else {
		return res;
	}

	const bool *mouse_bool_arrays[3] = { input->mouse_pressed, input->mouse_released, input->mouse_down };
	for (i32 i=0; i<3; i++) {
		Optional_U64 len = serialize_bools(mouse_bool_arrays[i], AU_MOUSE_BUTTON_COUNT, p, remain);
		if (len.some) {
			p += len.val;
			remain -= (i64) len.val;
		} else {
			return res;
		}
	}

	if (remain >= sizeof(i32)*4 + sizeof(float)*2) {
		*((i32 *) p) = input->pointer_x;
		p += sizeof(i32);
		*((i32 *) p) = input->pointer_y;
		p += sizeof(i32);
		*((i32 *) p) = input->pointer_dx;
		p += sizeof(i32);
		*((i32 *) p) = input->pointer_dy;
		p += sizeof(i32);
		*((float *) p) = input->wheel_dx;
		p += sizeof(float);
		*((float *) p) = input->wheel_dy;
		p += sizeof(float);
		remain -= sizeof(i32)*4 + sizeof(float)*2;
	} else {
		return res;
	}

	const bool *key_bool_arrays[3] = { input->key_pressed, input->key_released, input->key_down };
	for (i32 i=0; i<3; i++) {
		Optional_U64 len = serialize_bools(key_bool_arrays[i], AU_KEY_COUNT, p, remain);
		if (len.some) {
			p += len.val;
			remain -= (i64) len.val;
		} else {
			return res;
		}
	}

	Optional_U64 len = serialize_char_dynarray(input->text_entered, p, remain);
	if (len.some) {
		p += len.val;
		remain -= (i64) len.val;
	} else {
		return res;
	}

	if (remain >= sizeof(u64)*5 + sizeof(double)*2) {
		*((u64 *) p) = input->start_time;
		p += sizeof(u64);
		*((u64 *) p) = input->anim_time;
		p += sizeof(u64);
		*((u64 *) p) = input->wall_time;
		p += sizeof(u64);
		*((u64 *) p) = input->anim_dt;
		p += sizeof(u64);
		*((u64 *) p) = input->wall_dt;
		p += sizeof(u64);
		*((double *) p) = input->anim_dt_s;
		p += sizeof(double);
		*((double *) p) = input->wall_dt_s;
		p += sizeof(double);
		remain -= sizeof(u64)*5 + sizeof(double)*2;
	} else {
		return res;
	}

	return (Optional_U64) { buffer_size - remain, true };
}

Optional_U64 input_state_deserialize(const u8 *buffer, u64 buffer_size, Input_State *input)
{
	Optional_U64 res = { 0 };
	const u8 *p = buffer;
	u8 flags = *p++;
	input->quit = flags & 0x1;
	input->focus_gained = flags & 0x2;
	input->focus_lost = flags & 0x4;
	input->window_resized = flags & 0x8;
	input->window_rescaled = flags & 0x10;

	bool *mouse_bool_arrays[3] = { input->mouse_pressed, input->mouse_released, input->mouse_down };
	for (i32 i=0; i<3; i++) {
		Optional_U64 n = deserialize_bools(p, mouse_bool_arrays[i]);
		if (!n.some)
			return res;
		else
			p += n.val;
	}

	input->pointer_x = * (i32 *) p;
	p += sizeof(i32);
	input->pointer_y = * (i32 *) p;
	p += sizeof(i32);
	input->pointer_dx = * (i32 *) p;
	p += sizeof(i32);
	input->pointer_dy = * (i32 *) p;
	p += sizeof(i32);
	input->wheel_dx = * (float *) p;
	p += sizeof(float);
	input->wheel_dy = * (float *) p;
	p += sizeof(float);

	bool *key_bool_arrays[3] = { input->key_pressed, input->key_released, input->key_down };
	for (i32 i=0; i<3; i++) {
		Optional_U64 n = deserialize_bools(p, key_bool_arrays[i]);
		if (!n.some)
			return res;
		else
			p += n.val;
	}

	Optional_U64 n = deserialize_char_dynarray(p, input->text_entered);
	if (!n.some)
		return res;
	else
		p += n.val;

	input->start_time = * (u64 *) p;
	p += sizeof(u64);
	input->anim_time = * (u64 *) p;
	p += sizeof(u64);
	input->wall_time = * (u64 *) p;
	p += sizeof(u64);
	input->anim_dt = * (u64 *) p;
	p += sizeof(u64);
	input->wall_dt = * (u64 *) p;
	p += sizeof(u64);
	input->anim_dt_s = * (double *) p;
	p += sizeof(double);
	input->wall_dt_s = * (double *) p;
	p += sizeof(double);

	return (Optional_U64) { p - buffer, true };
}

bool au_window_record_input(Window *window, const char *out_path)
{
	if (!g_input_recording_file) {
		if (window->input_mode == AU_INPUT_MODE_PLAYBACK) {
			au_window_stop_playback(window);
		}
		g_input_recording_file = fopen(out_path, "wb+");
		if (!g_input_recording_file) {
			return false;
		}
		window->input_mode = AU_INPUT_MODE_RECORDING;
		printf("Recording input to %s\n", out_path);
		return true;
	} else {
		// Todo: only one window can record or playback input at time
		return false;
	}
}

bool au_window_stop_recording(Window *window)
{
	if (window->input_mode == AU_INPUT_MODE_RECORDING) {
		window->input_mode = AU_INPUT_MODE_NORMAL;
		bool ok = fclose(g_input_recording_file);
		g_input_recording_file = NULL;
		return ok;
	} else {
		return false;
	}
}

bool au_window_playback_input(Window *window, const char *in_path)
{
	if (!g_input_recording_file) {
		if (window->input_mode == AU_INPUT_MODE_RECORDING) {
			au_window_stop_recording(window);
		}
		g_input_recording_file = fopen(in_path, "rb");
		if (!g_input_recording_file) {
			return false;
		}
		window->input_mode = AU_INPUT_MODE_PLAYBACK;
		return true;
	} else {
		// Todo: only one window can record or playback input at time
		return false;
	}
}

bool au_window_stop_playback(Window *window)
{
	if (window->input_mode == AU_INPUT_MODE_PLAYBACK) {
		window->input_mode = AU_INPUT_MODE_NORMAL;
		bool ok = fclose(g_input_recording_file);
		g_input_recording_file = NULL;
		return ok;
	} else {
		return false;
	}
}

bool input_state_serialize_and_write(Window *window)
{
	if (g_input_recording_file) {
		u64 buf_size = input_state_max_serial_size(window->input);
		u8 *buf = adalloc(window->arena, buf_size);
		if (!buf)
			return false;

		Optional_U64 size = input_state_serialize(window->input, buf, buf_size);
		if (!size.some)
			goto err;

		// write size first
		u64 n = fwrite(&size.val, sizeof(u64), 1, g_input_recording_file);
		if (!n)
			goto err;

		n = fwrite(buf, 1, size.val, g_input_recording_file);
		if (!n)
			goto err;

		// prevent data loss on crash this frame
		if (fflush(g_input_recording_file))
			goto err;

		afree(window->arena, buf);
		return true;

		err:
		afree(window->arena, buf);
		return false;
	} else {
		return false;
	}
}

bool input_state_read_and_deserialize(Window *window)
{
	if (g_input_recording_file) {
		// read size
		u64 size;
		u64 n = fread(&size, sizeof(u64), 1, g_input_recording_file);
		if (n != 1 || (size > 1 << 20)) {
			// catch broken sizes in dev
			dev_assertf(n != sizeof(u64), NULL);
			return false;
		}
		u8 *buf = adalloc(window->arena, size);
		n = fread(buf, 1, size, g_input_recording_file);
		if (n != size) {
			goto err;
		}
		Optional_U64 len = input_state_deserialize(buf, size, window->input);
		if (!len.some)
			goto err;

		afree(window->arena, buf);
		return true;

		err:
			afree(window->arena, buf);
			return false;
	} else {
		return false;
	}
}

// bool
