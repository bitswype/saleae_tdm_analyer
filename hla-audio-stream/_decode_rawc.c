/*
 * _decode_rawc.c -- Raw Python C extension for TDM Audio Stream HLA decode hot path.
 *
 * Provides a RawCDecoder type that implements the same interface as
 * FastDecoder (Cython) and CffiDecoder (cffi), but uses the Python C API
 * directly for maximum performance.
 *
 * Build:
 *     cd hla-audio-stream
 *     python setup_rawc.py build_ext --inplace
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <string.h>

/* =========================================================================
 * DecoderState -- mirrors the cffi struct exactly
 * ========================================================================= */

typedef struct {
    int slot_set[256];
    int slot_list[256];
    int n_slots;

    long long sign_mask;
    long long sign_threshold;
    long long sign_subtract;

    long long last_frame_num;
    int last_frame_num_valid;

    long long accum[256];
    int accum_valid[256];

    unsigned char *batch_buf;
    int batch_offset;
    int batch_count;
    int batch_size;
    int frame_byte_size;
    int bytes_per_sample;

    long long frame_count;
    int sample_rate_known;
} DecoderState;

/* =========================================================================
 * pack_frame -- pack accumulated samples into the batch buffer (pure C)
 * ========================================================================= */

static void pack_frame(DecoderState *state) {
    int i, s;
    long long sample;
    int offset = state->batch_offset;
    unsigned char *buf = state->batch_buf;

    for (i = 0; i < state->n_slots; i++) {
        s = state->slot_list[i];
        sample = state->accum_valid[s] ? state->accum[s] : 0;

        if (state->bytes_per_sample == 2) {
            short s16 = (short)sample;
            buf[offset]     = (unsigned char)(s16 & 0xFF);
            buf[offset + 1] = (unsigned char)((s16 >> 8) & 0xFF);
            offset += 2;
        } else {
            int s32 = (int)sample;
            buf[offset]     = (unsigned char)(s32 & 0xFF);
            buf[offset + 1] = (unsigned char)((s32 >> 8) & 0xFF);
            buf[offset + 2] = (unsigned char)((s32 >> 16) & 0xFF);
            buf[offset + 3] = (unsigned char)((s32 >> 24) & 0xFF);
            offset += 4;
        }
    }
    state->batch_offset = offset;
    state->batch_count++;
    state->frame_count++;
}

/* =========================================================================
 * decoder_process_frame -- the C hot loop (same logic as cffi version)
 * ========================================================================= */

static int decoder_process_frame(DecoderState *state, int slot, long long frame_num,
                                 long long data, int has_error) {
    int i;
    int flush_needed = 0;

    /* Slot filter */
    if (slot < 0 || slot > 255 || !state->slot_set[slot])
        return -1;

    /* Frame boundary detection */
    if (state->last_frame_num_valid) {
        if (frame_num != state->last_frame_num) {
            if (state->sample_rate_known) {
                pack_frame(state);
                if (state->batch_count >= state->batch_size)
                    flush_needed = 1;
            }
            /* Reset accumulator */
            for (i = 0; i < 256; i++)
                state->accum_valid[i] = 0;
        }
    }

    state->last_frame_num = frame_num;
    state->last_frame_num_valid = 1;

    /* Accumulate sample (skip if error) */
    if (!has_error) {
        long long v = data & state->sign_mask;
        if (v >= state->sign_threshold)
            v -= state->sign_subtract;
        state->accum[slot] = v;
        state->accum_valid[slot] = 1;
    }

    return flush_needed ? 1 : 0;
}

/* =========================================================================
 * RawCDecoder Python type
 * ========================================================================= */

typedef struct {
    PyObject_HEAD
    DecoderState state;
    PyObject *batch_buf_py;   /* Python bytearray owning the buffer memory */
    PyObject *slot_list_py;   /* Keep a reference to the Python slot list */
} RawCDecoderObject;

/* -- tp_new ------------------------------------------------------------ */

static PyObject *
RawCDecoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    RawCDecoderObject *self;
    self = (RawCDecoderObject *)type->tp_alloc(type, 0);
    if (self != NULL) {
        memset(&self->state, 0, sizeof(DecoderState));
        self->batch_buf_py = NULL;
        self->slot_list_py = NULL;
    }
    return (PyObject *)self;
}

/* -- tp_init ----------------------------------------------------------- */

static int
RawCDecoder_init(RawCDecoderObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *slot_list_obj;
    int bit_depth, batch_size, frame_byte_size;
    Py_ssize_t n_slots, i;

    static char *kwlist[] = {"slot_list", "bit_depth", "batch_size",
                             "frame_byte_size", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oiii", kwlist,
                                      &slot_list_obj, &bit_depth,
                                      &batch_size, &frame_byte_size))
        return -1;

    if (!PyList_Check(slot_list_obj)) {
        PyErr_SetString(PyExc_TypeError, "slot_list must be a list");
        return -1;
    }

    n_slots = PyList_GET_SIZE(slot_list_obj);
    if (n_slots > 256) {
        PyErr_SetString(PyExc_ValueError, "slot_list too long (max 256)");
        return -1;
    }

    /* Initialize state */
    memset(&self->state, 0, sizeof(DecoderState));

    self->state.n_slots = (int)n_slots;
    for (i = 0; i < n_slots; i++) {
        PyObject *item = PyList_GET_ITEM(slot_list_obj, i);  /* borrowed ref */
        int slot = (int)PyLong_AsLong(item);
        if (slot == -1 && PyErr_Occurred())
            return -1;
        self->state.slot_list[i] = slot;
        if (slot >= 0 && slot < 256)
            self->state.slot_set[slot] = 1;
    }

    self->state.bytes_per_sample = (bit_depth > 16) ? 4 : 2;
    self->state.sign_mask = ((long long)1 << bit_depth) - 1;
    self->state.sign_threshold = (long long)1 << (bit_depth - 1);
    self->state.sign_subtract = (long long)1 << bit_depth;

    self->state.batch_size = batch_size;
    self->state.frame_byte_size = frame_byte_size;

    /* Allocate batch buffer as a Python bytearray */
    Py_XDECREF(self->batch_buf_py);
    self->batch_buf_py = PyByteArray_FromStringAndSize(NULL,
                             (Py_ssize_t)batch_size * frame_byte_size);
    if (!self->batch_buf_py)
        return -1;

    /* Zero the buffer */
    memset(PyByteArray_AS_STRING(self->batch_buf_py), 0,
           (size_t)batch_size * frame_byte_size);

    self->state.batch_buf = (unsigned char *)PyByteArray_AS_STRING(self->batch_buf_py);

    /* Keep a reference to the slot list */
    Py_XDECREF(self->slot_list_py);
    Py_INCREF(slot_list_obj);
    self->slot_list_py = slot_list_obj;

    return 0;
}

/* -- tp_dealloc -------------------------------------------------------- */

static void
RawCDecoder_dealloc(RawCDecoderObject *self)
{
    Py_XDECREF(self->batch_buf_py);
    Py_XDECREF(self->slot_list_py);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* -- process_frame ----------------------------------------------------- */

static PyObject *
RawCDecoder_process_frame(RawCDecoderObject *self, PyObject *frame)
{
    PyObject *frame_type = NULL;
    PyObject *data_dict = NULL;
    PyObject *result_obj = NULL;
    int is_slot;

    /* Get frame.type (new ref) */
    frame_type = PyObject_GetAttrString(frame, "type");
    if (!frame_type)
        return NULL;

    /* Compare to "slot" using PyUnicode_CompareWithASCIIString */
    is_slot = (PyUnicode_CompareWithASCIIString(frame_type, "slot") == 0);
    Py_DECREF(frame_type);
    if (!is_slot)
        return PyLong_FromLong(-1);

    /* Get frame.data dict (new ref) */
    data_dict = PyObject_GetAttrString(frame, "data");
    if (!data_dict)
        return NULL;

    /* Extract slot -- borrowed ref from PyDict_GetItemString */
    PyObject *slot_obj = PyDict_GetItemString(data_dict, "slot");
    PyObject *fn_obj = PyDict_GetItemString(data_dict, "frame_number");
    if (!slot_obj || !fn_obj) {
        Py_DECREF(data_dict);
        return PyLong_FromLong(-1);
    }

    long long slot = PyLong_AsLongLong(slot_obj);
    if (slot == -1 && PyErr_Occurred()) {
        Py_DECREF(data_dict);
        return NULL;
    }
    long long frame_num = PyLong_AsLongLong(fn_obj);
    if (frame_num == -1 && PyErr_Occurred()) {
        Py_DECREF(data_dict);
        return NULL;
    }

    /* Error flags (may be missing -- PyDict_GetItemString returns NULL) */
    int has_error = 0;
    PyObject *ss = PyDict_GetItemString(data_dict, "short_slot");   /* borrowed */
    PyObject *be = PyDict_GetItemString(data_dict, "bitclock_error"); /* borrowed */
    if ((ss && PyObject_IsTrue(ss)) || (be && PyObject_IsTrue(be)))
        has_error = 1;

    /* Data value (may be missing) */
    long long raw_data = 0;
    if (!has_error) {
        PyObject *data_obj = PyDict_GetItemString(data_dict, "data"); /* borrowed */
        if (data_obj) {
            raw_data = PyLong_AsLongLong(data_obj);
            if (raw_data == -1 && PyErr_Occurred()) {
                Py_DECREF(data_dict);
                return NULL;
            }
        }
    }

    Py_DECREF(data_dict);

    /* Call the pure C hot loop */
    int c_result = decoder_process_frame(&self->state, (int)slot, frame_num,
                                         raw_data, has_error);

    result_obj = PyLong_FromLong(c_result);
    return result_obj;
}

/* -- set_sample_rate_known --------------------------------------------- */

static PyObject *
RawCDecoder_set_sample_rate_known(RawCDecoderObject *self, PyObject *Py_UNUSED(ignored))
{
    self->state.sample_rate_known = 1;
    Py_RETURN_NONE;
}

/* -- get_batch_data ---------------------------------------------------- */

static PyObject *
RawCDecoder_get_batch_data(RawCDecoderObject *self, PyObject *Py_UNUSED(ignored))
{
    if (self->state.batch_count == 0)
        return PyBytes_FromStringAndSize("", 0);

    return PyBytes_FromStringAndSize(
        (const char *)self->state.batch_buf,
        (Py_ssize_t)self->state.batch_offset);
}

/* -- reset_batch ------------------------------------------------------- */

static PyObject *
RawCDecoder_reset_batch(RawCDecoderObject *self, PyObject *Py_UNUSED(ignored))
{
    self->state.batch_offset = 0;
    self->state.batch_count = 0;
    Py_RETURN_NONE;
}

/* -- get_accum --------------------------------------------------------- */

static PyObject *
RawCDecoder_get_accum(RawCDecoderObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *dict = PyDict_New();
    if (!dict)
        return NULL;

    int i;
    for (i = 0; i < self->state.n_slots; i++) {
        int s = self->state.slot_list[i];
        if (s >= 0 && s < 256 && self->state.accum_valid[s]) {
            PyObject *key = PyLong_FromLong(s);
            PyObject *val = PyLong_FromLongLong(self->state.accum[s]);
            if (!key || !val) {
                Py_XDECREF(key);
                Py_XDECREF(val);
                Py_DECREF(dict);
                return NULL;
            }
            PyDict_SetItem(dict, key, val);
            Py_DECREF(key);
            Py_DECREF(val);
        }
    }
    return dict;
}

/* =========================================================================
 * Properties: frame_count, batch_count, last_frame_num
 * ========================================================================= */

static PyObject *
RawCDecoder_get_frame_count(RawCDecoderObject *self, void *closure)
{
    return PyLong_FromLongLong(self->state.frame_count);
}

static PyObject *
RawCDecoder_get_batch_count(RawCDecoderObject *self, void *closure)
{
    return PyLong_FromLong(self->state.batch_count);
}

static PyObject *
RawCDecoder_get_last_frame_num(RawCDecoderObject *self, void *closure)
{
    if (self->state.last_frame_num_valid)
        return PyLong_FromLongLong(self->state.last_frame_num);
    Py_RETURN_NONE;
}

/* =========================================================================
 * Type tables
 * ========================================================================= */

static PyMethodDef RawCDecoder_methods[] = {
    {"process_frame",          (PyCFunction)RawCDecoder_process_frame,
     METH_O,                   "Process one FrameV2. Returns 0, 1 (flush needed), or -1 (filtered)."},
    {"set_sample_rate_known",  (PyCFunction)RawCDecoder_set_sample_rate_known,
     METH_NOARGS,              "Signal that sample rate has been derived."},
    {"get_batch_data",         (PyCFunction)RawCDecoder_get_batch_data,
     METH_NOARGS,              "Return batch buffer contents as bytes."},
    {"reset_batch",            (PyCFunction)RawCDecoder_reset_batch,
     METH_NOARGS,              "Reset batch buffer after flush."},
    {"get_accum",              (PyCFunction)RawCDecoder_get_accum,
     METH_NOARGS,              "Return accumulator as dict {slot: signed_value}."},
    {NULL}
};

static PyGetSetDef RawCDecoder_getset[] = {
    {"frame_count",    (getter)RawCDecoder_get_frame_count,    NULL,
     "Number of complete PCM frames packed.",  NULL},
    {"batch_count",    (getter)RawCDecoder_get_batch_count,    NULL,
     "Number of frames in current batch.",     NULL},
    {"last_frame_num", (getter)RawCDecoder_get_last_frame_num, NULL,
     "Last processed TDM frame number.",       NULL},
    {NULL}
};

static PyTypeObject RawCDecoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "_decode_rawc.RawCDecoder",
    .tp_doc       = "Raw C decoder for TDM Audio Stream HLA hot path.",
    .tp_basicsize = sizeof(RawCDecoderObject),
    .tp_itemsize  = 0,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_new       = RawCDecoder_new,
    .tp_init      = (initproc)RawCDecoder_init,
    .tp_dealloc   = (destructor)RawCDecoder_dealloc,
    .tp_methods   = RawCDecoder_methods,
    .tp_getset    = RawCDecoder_getset,
};

/* =========================================================================
 * Module definition
 * ========================================================================= */

static struct PyModuleDef decode_rawc_module = {
    PyModuleDef_HEAD_INIT,
    "_decode_rawc",
    "Raw C extension for TDM Audio Stream HLA decode hot path.",
    -1,
    NULL,  /* no module-level methods */
};

PyMODINIT_FUNC
PyInit__decode_rawc(void)
{
    PyObject *m;

    if (PyType_Ready(&RawCDecoderType) < 0)
        return NULL;

    m = PyModule_Create(&decode_rawc_module);
    if (m == NULL)
        return NULL;

    Py_INCREF(&RawCDecoderType);
    if (PyModule_AddObject(m, "RawCDecoder", (PyObject *)&RawCDecoderType) < 0) {
        Py_DECREF(&RawCDecoderType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
