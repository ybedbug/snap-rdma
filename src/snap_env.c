#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "snap_env.h"

#define SNA_ENV_DUMP_FORMAT "%-22s : %-7s : %-3d"
#define SNA_ENV_MAX_ENTRIES_NUM 8

struct mlnx_snap_env {
        const char *name;
        int defined;    
        int default_val;
};

static int last_used_idx = 0;
static struct mlnx_snap_env env_arr[SNA_ENV_MAX_ENTRIES_NUM + 1] = {{0}};

static void snap_env_set_val(struct mlnx_snap_env *env)
{
        if (getenv(env->name)) {
                env->defined = 1;
                env->default_val = atoi(getenv(env->name));
        }
}

static int snap_env_dump_one_entry(struct mlnx_snap_env *env, 
                                   char *buf, 
                                   unsigned int buf_size)
{
        int written = snprintf(buf, buf_size, SNA_ENV_DUMP_FORMAT,
                               env->name, env->defined ? "set" : "not set",
                               env->default_val
                               );
        return written;
}

static struct mlnx_snap_env* snap_env_find(const char *env_name)
{
        struct mlnx_snap_env *env = env_arr;
        while (env->name) {
                if (!strcmp(env_name, env->name))
                        return env;
                ++env;
        }        
        return NULL;
}

int snap_env_add(const char *env_name, int default_val)
{
        struct mlnx_snap_env *env = snap_env_find(env_name);

        if (env) // already added
                return 0;

        if (last_used_idx < SNA_ENV_MAX_ENTRIES_NUM) {
                env = &env_arr[last_used_idx++];
                env->name = env_name;
                env->default_val = default_val;
                snap_env_set_val(env);

                return 0;
        }

        return -EINVAL;
}

int snap_env_getenv(const char *env_name)
{
        const struct mlnx_snap_env *env = snap_env_find(env_name);
        if (env && env->defined)
                return env->default_val;
        
        return -EINVAL;
}

int snap_env_is_set(const char *env_name)
{
        const struct mlnx_snap_env *env = snap_env_find(env_name);
        return env ? env->defined : 0;
}

snap_env_iter_t snap_env_dump_env_entry(snap_env_iter_t iter, char *buf, unsigned int buf_size)
{      
        if (buf && buf_size) {
                struct mlnx_snap_env *env = iter ? (struct mlnx_snap_env *)iter : env_arr;
                if (env && env->name) {
                        snap_env_dump_one_entry(env, buf, buf_size);
                        ++env;
                        return env->name ? env : NULL;
                }
        }
        return NULL;
}
