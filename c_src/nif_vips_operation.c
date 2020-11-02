#include <glib-object.h>
#include <stdio.h>
#include <vips/vips.h>

#include "nif_g_boxed.h"
#include "nif_g_object.h"
#include "nif_g_param_spec.h"
#include "nif_g_value.h"
#include "nif_vips_boxed.h"
#include "nif_vips_operation.h"
#include "vix_utils.h"

/* VipsArgumentFlags */
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_NONE;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_REQUIRED;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_CONSTRUCT;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_SET_ONCE;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_SET_ALWAYS;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_INPUT;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_OUTPUT;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_DEPRECATED;
ERL_NIF_TERM ATOM_VIPS_ARGUMENT_MODIFY;

typedef struct NifVipsOperationsList {
  GType *gtype;
  unsigned int count;
} NifVipsOperationsList;

static ERL_NIF_TERM vips_argument_flags_to_erl_terms(ErlNifEnv *env,
                                                     int flags) {
  ERL_NIF_TERM erl_terms[8];
  int len = 0;

  if (flags & VIPS_ARGUMENT_REQUIRED) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_REQUIRED;
    len++;
  }

  if (flags & VIPS_ARGUMENT_CONSTRUCT) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_CONSTRUCT;
    len++;
  }

  if (flags & VIPS_ARGUMENT_SET_ONCE) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_SET_ONCE;
    len++;
  }

  if (flags & VIPS_ARGUMENT_SET_ALWAYS) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_SET_ALWAYS;
    len++;
  }

  if (flags & VIPS_ARGUMENT_INPUT) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_INPUT;
    len++;
  }

  if (flags & VIPS_ARGUMENT_OUTPUT) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_OUTPUT;
    len++;
  }

  if (flags & VIPS_ARGUMENT_DEPRECATED) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_DEPRECATED;
    len++;
  }

  if (flags & VIPS_ARGUMENT_MODIFY) {
    erl_terms[len] = ATOM_VIPS_ARGUMENT_MODIFY;
    len++;
  }

  return enif_make_list_from_array(env, erl_terms, len);
}

static VixResult get_operation_properties(ErlNifEnv *env, VipsOperation *op) {

  VixResult result;
  GValue gvalue = {0};
  GObject *g_object;

  const char **names;
  int *flags;
  int n_args = 0;

  if (vips_object_get_args(VIPS_OBJECT(op), &names, &flags, &n_args)) {
    error("failed to get args. error: %s", vips_error_buffer());
    vips_error_clear();
    result.success = false;
    result.term = enif_make_badarg(env);
    return result;
  }

  ERL_NIF_TERM list;
  GParamSpec *pspec;
  VipsArgumentClass *arg_class;
  VipsArgumentInstance *arg_instance;

  list = enif_make_list(env, 0);

  for (int i = 0; i < n_args; i++) {
    if (flags[i] & VIPS_ARGUMENT_OUTPUT) {
      if (vips_object_get_argument(VIPS_OBJECT(op), names[i], &pspec,
                                   &arg_class, &arg_instance)) {
        error("failed to get argument: %s", names[i]);
        result.success = false;
        result.term = raise_exception(env, "failed to get argument");
        return result;
      }

      g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(pspec));
      g_object_get_property(G_OBJECT(op), names[i], &gvalue);

      g_object = g_value_get_object(&gvalue);
      list =
          enif_make_list_cell(env, g_object_to_erl_term(env, g_object), list);
      g_value_unset(&gvalue);
    }
  }

  result.success = true;
  result.term = list;

  return result;
}

static VixResult set_operation_properties(ErlNifEnv *env, VipsOperation *op,
                                          ERL_NIF_TERM list) {

  VixResult result;
  unsigned int length = 0;

  if (!enif_get_list_length(env, list, &length)) {
    error("Failed to get list length");
    result.success = false;
    result.term = enif_make_badarg(env);
    return result;
  }

  ERL_NIF_TERM head;
  const ERL_NIF_TERM *tup;
  int count;

  char name[1024];
  GParamSpec *pspec;
  VipsArgumentClass *arg_class;
  VipsArgumentInstance *arg_instance;

  GValue gvalue = {0};

  for (unsigned int i = 0; i < length; i++) {
    if (!enif_get_list_cell(env, list, &head, &list)) {
      error("Failed to get list");
      result.success = false;
      result.term = enif_make_badarg(env);
      return result;
    }

    if (!enif_get_tuple(env, head, &count, &tup)) {
      error("Failed to get tuple");
      result.success = false;
      result.term = enif_make_badarg(env);
      return result;
    }

    if (count != 2) {
      error("Tuple length must be 2");
      result.success = false;
      result.term = enif_make_badarg(env);
      return result;
    }

    if (enif_get_string(env, tup[0], name, 1024, ERL_NIF_LATIN1) < 0) {
      error("failed to get param name");
      result.success = false;
      result.term = enif_make_badarg(env);
      return result;
    }

    if (vips_object_get_argument(VIPS_OBJECT(op), name, &pspec, &arg_class,
                                 &arg_instance)) {
      error("failed to get argument: %s", name);
      result.success = false;
      result.term = raise_exception(env, "failed to get argument");
      return result;
    }

    GValueResult res = set_g_value_from_erl_term(env, pspec, tup[1], &gvalue);
    if (!res.is_success) {
      result.success = false;
      result.term = res.term;
      return result;
    }

    g_object_set_property(G_OBJECT(op), name, &gvalue);
    g_value_unset(&gvalue);
  }

  result.success = true;
  return result;
}

ERL_NIF_TERM nif_vips_operation_call(ErlNifEnv *env, int argc,
                                     const ERL_NIF_TERM argv[]) {
  ERL_NIF_TERM result;
  VipsOperation *op = NULL;

  assert_argc(argc, 2);

  char op_name[200] = {'\0'};
  if (enif_get_string(env, argv[0], op_name, 200, ERL_NIF_LATIN1) < 1) {
    error("operation name must be a valid string");
    result = enif_make_badarg(env);
    goto exit;
  }

  op = vips_operation_new(op_name);

  debug("created operation");

  VixResult op_result = set_operation_properties(env, op, argv[1]);
  if (!op_result.success) {
    result = op_result.term;
    goto exit;
  }

  debug("set operation properties");

  VipsOperation *new_op;
  if (!(new_op = vips_cache_operation_build(op))) {
    error("failed to build operation, error: %s", vips_error_buffer());
    vips_error_clear();
    result = raise_exception(env, "failed to build operation");
    goto exit;
  }

  debug("ran operation");

  g_object_unref(op);
  op = new_op;

  op_result = get_operation_properties(env, op);
  if (!op_result.success) {
    error("NIF Vips Operation get operation properties failed");
    result = op_result.term;
    goto exit;
  }
  debug("got operation properties");
  result = op_result.term;

exit:
  if (op) {
    vips_object_unref_outputs(VIPS_OBJECT(op));
    g_object_unref(op);
  }
  return result;
}

ERL_NIF_TERM nif_vips_operation_get_arguments(ErlNifEnv *env, int argc,
                                              const ERL_NIF_TERM argv[]) {

  assert_argc(argc, 1);

  VipsOperation *op;
  char op_name[200] = {0};
  const char **names;
  int *flags;
  int n_args = 0;
  ERL_NIF_TERM terms[n_args];
  ERL_NIF_TERM erl_flags, name, priority;
  GParamSpec *pspec;
  VipsArgumentClass *arg_class;
  VipsArgumentInstance *arg_instance;

  if (enif_get_string(env, argv[0], op_name, 200, ERL_NIF_LATIN1) < 1)
    return raise_badarg(env, "operation name must be a valid string");

  op = vips_operation_new(op_name);

  if (vips_object_get_args(VIPS_OBJECT(op), &names, &flags, &n_args) != 0) {
    error("failed to get VipsObject arguments. error: %s", vips_error_buffer());
    vips_error_clear();
    return raise_exception(env, "failed to get VipsObject arguments");
  }

  for (int i = 0; i < n_args; i++) {
    name = enif_make_string(env, names[i], ERL_NIF_LATIN1);
    erl_flags = vips_argument_flags_to_erl_terms(env, flags[i]);
    priority = enif_make_int(env, arg_class->priority);

    if (vips_object_get_argument(VIPS_OBJECT(op), names[i], &pspec, &arg_class,
                                 &arg_instance)) {
      error("failed to get VipsObject argument. error: %s",
            vips_error_buffer());
      vips_error_clear();
      return raise_exception(env, "failed to get VipsObject argument");
    }

    terms[i] = enif_make_tuple4(env, name, g_param_spec_details(env, pspec),
                                priority, erl_flags);
  }

  /* g_free(names); */
  /* g_free(flags); */

  return enif_make_list_from_array(env, terms, n_args);
}

static void *list_class(GType type, void *user_data) {
  VipsObjectClass *class = VIPS_OBJECT_CLASS(g_type_class_ref(type));

  if (class->deprecated)
    return (NULL);
  if (VIPS_IS_OPERATION_CLASS(class) &&
      (VIPS_OPERATION_CLASS(class)->flags & VIPS_OPERATION_DEPRECATED))
    return (NULL);
  if (G_TYPE_IS_ABSTRACT(type))
    return (NULL);

  NifVipsOperationsList *list = (NifVipsOperationsList *)user_data;

  list->gtype[list->count] = type;
  list->count = list->count + 1;

  return (NULL);
}

ERL_NIF_TERM nif_vips_operation_list(ErlNifEnv *env, int argc,
                                     const ERL_NIF_TERM argv[]) {

  assert_argc(argc, 0);

  GType _gtype[1024], gtype;
  NifVipsOperationsList list;
  ERL_NIF_TERM erl_term, description, nickname, op_usage;
  gpointer g_class;
  VipsOperationClass *op_class;

  char str[4096];
  VipsBuf buf = VIPS_BUF_STATIC(str);

  list.gtype = (GType *)&_gtype;
  list.count = 0;

  vips_type_map_all(g_type_from_name("VipsOperation"), list_class, &list);

  erl_term = enif_make_list(env, 0);

  for (unsigned int i = 0; i < list.count; i++) {
    gtype = list.gtype[i];
    g_class = g_type_class_ref(gtype);

    vips_buf_rewind(&buf);
    vips_object_summary_class(VIPS_OBJECT_CLASS(g_class), &buf);
    description = enif_make_string(env, vips_buf_all(&buf), ERL_NIF_LATIN1);

    vips_buf_rewind(&buf);
    op_class = VIPS_OPERATION_CLASS(g_class);
    op_class->usage(op_class, &buf);
    op_usage = enif_make_string(env, vips_buf_all(&buf), ERL_NIF_LATIN1);

    nickname = enif_make_string(env, vips_nickname_find(list.gtype[i]),
                                ERL_NIF_LATIN1);

    erl_term = enif_make_list_cell(
        env, enif_make_tuple3(env, nickname, description, op_usage), erl_term);

    g_type_class_unref(g_class);
  }

  return erl_term;
}

ERL_NIF_TERM nif_vips_cache_set_max(ErlNifEnv *env, int argc,
                                    const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 1);

  int max_op;

  if (!enif_get_int(env, argv[0], &max_op)) {
    return raise_badarg(env, "Failed to integer value");
  }

  vips_cache_set_max(max_op);
  return ATOM_OK;
}

ERL_NIF_TERM nif_vips_cache_get_max(ErlNifEnv *env, int argc,
                                    const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 0);
  return make_ok(env, enif_make_int(env, vips_cache_get_max()));
}

ERL_NIF_TERM nif_vips_concurrency_set(ErlNifEnv *env, int argc,
                                      const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 1);

  int concurrency;

  if (!enif_get_int(env, argv[0], &concurrency)) {
    return raise_badarg(env, "Failed to integer value");
  }

  vips_concurrency_set(concurrency);
  return ATOM_OK;
}

ERL_NIF_TERM nif_vips_concurrency_get(ErlNifEnv *env, int argc,
                                      const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 0);
  return make_ok(env, enif_make_int(env, vips_concurrency_get()));
}

ERL_NIF_TERM nif_vips_cache_set_max_files(ErlNifEnv *env, int argc,
                                          const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 1);

  int max_files;

  if (!enif_get_int(env, argv[0], &max_files)) {
    return raise_badarg(env, "Failed to integer value");
  }

  vips_cache_set_max_files(max_files);
  return ATOM_OK;
}

ERL_NIF_TERM nif_vips_cache_get_max_files(ErlNifEnv *env, int argc,
                                          const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 0);
  return make_ok(env, enif_make_int(env, vips_cache_get_max_files()));
}

ERL_NIF_TERM nif_vips_cache_set_max_mem(ErlNifEnv *env, int argc,
                                        const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 1);

  unsigned long max_mem;

  if (!enif_get_uint64(env, argv[0], &max_mem)) {
    return raise_badarg(env, "Failed to integer value");
  }

  vips_cache_set_max_mem(max_mem);
  return ATOM_OK;
}

ERL_NIF_TERM nif_vips_cache_get_max_mem(ErlNifEnv *env, int argc,
                                        const ERL_NIF_TERM argv[]) {
  assert_argc(argc, 0);
  return make_ok(env, enif_make_uint64(env, vips_cache_get_max_mem()));
}

ERL_NIF_TERM nif_vips_operation_init(ErlNifEnv *env) {
  ATOM_VIPS_ARGUMENT_NONE = enif_make_atom(env, "vips_argument_none");
  ATOM_VIPS_ARGUMENT_REQUIRED = enif_make_atom(env, "vips_argument_required");
  ATOM_VIPS_ARGUMENT_CONSTRUCT = enif_make_atom(env, "vips_argument_construct");
  ATOM_VIPS_ARGUMENT_SET_ONCE = enif_make_atom(env, "vips_argument_set_once");
  ATOM_VIPS_ARGUMENT_SET_ALWAYS =
      enif_make_atom(env, "vips_argument_set_always");
  ATOM_VIPS_ARGUMENT_INPUT = enif_make_atom(env, "vips_argument_input");
  ATOM_VIPS_ARGUMENT_OUTPUT = enif_make_atom(env, "vips_argument_output");
  ATOM_VIPS_ARGUMENT_DEPRECATED =
      enif_make_atom(env, "vips_argument_deprecated");
  ATOM_VIPS_ARGUMENT_MODIFY = enif_make_atom(env, "vips_argument_modify");

  if (!G_BOXED_RT)
    return raise_exception(env, "Failed to open g_boxed_resource");

  return ATOM_OK;
}
