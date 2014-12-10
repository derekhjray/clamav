/*
 *  Copyright (C) 2009-2013 Sourcefire, Inc.
 *  Author: Tomasz Kojm <tkojm@clamav.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <time.h>
#ifdef HAVE_UNAME_SYSCALL
#include <sys/utsname.h>
#endif
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "shared/optparser.h"
#include "shared/misc.h"

#include "libclamav/str.h"
#include "libclamav/clamav.h"
#include "libclamav/others.h"
#include "libclamav/readdb.h"
#include "libclamav/bytecode.h"
#include "libclamav/bytecode_detect.h"
#include "target.h"
#include "fpu.h"

#ifndef _WIN32
extern const struct clam_option *clam_options;
#else
__declspec(dllimport) extern const struct clam_option *clam_options;
#endif

static struct _cfgfile {
    const char *name;
    int tool;
} cfgfile[] = {
    { "clamd.conf",	    OPT_CLAMD	    },
    { "freshclam.conf",	    OPT_FRESHCLAM   },
    { "clamav-milter.conf", OPT_MILTER	    },
    { NULL,		    0		    }
};

static void printopts(struct optstruct *opts, int nondef)
{
	const struct optstruct *opt;

    while(opts) {
	if(!opts->name) {
	    opts = opts->next;
	    continue;
	}
	if(clam_options[opts->idx].owner & OPT_DEPRECATED) {
	    if(opts->active)
		printf("*** %s is DEPRECATED ***\n", opts->name);
	    opts = opts->next;
	    continue;
	}
	if(nondef && (opts->numarg == clam_options[opts->idx].numarg) && ((opts->strarg == clam_options[opts->idx].strarg) || (opts->strarg && clam_options[opts->idx].strarg && !strcmp(opts->strarg, clam_options[opts->idx].strarg)))) {
	    opts = opts->next;
	    continue;
	}
	if(!opts->enabled) 
	    printf("%s disabled\n", opts->name);
	else switch(clam_options[opts->idx].argtype) {
	    case CLOPT_TYPE_STRING:
		printf("%s = \"%s\"", opts->name, opts->strarg);
		opt = opts;
		while((opt = opt->nextarg))
		    printf(", \"%s\"", opt->strarg);
		printf("\n");
		break;

	    case CLOPT_TYPE_NUMBER:
	    case CLOPT_TYPE_SIZE:
		printf("%s = \"%lld\"", opts->name, opts->numarg);
		opt = opts;
		while((opt = opt->nextarg))
		    printf(", \"%lld\"", opt->numarg);
		printf("\n");
		break;

	    case CLOPT_TYPE_BOOL:
		printf("%s = \"yes\"\n", opts->name);
		break;

	    default:
		printf("!!! %s: UNKNOWN INTERNAL TYPE !!!\n", opts->name);
	}
	opts = opts->next;
    }
}

static int printconf(const char *name)
{
	int i, j, tool = 0, tokens_count;
	char buffer[1025];
	const char *tokens[128];
	const struct clam_option *cpt;

    for(i = 0; cfgfile[i].name; i++) {
	if(!strcmp(name, cfgfile[i].name)) {
	    tool = cfgfile[i].tool;
	    break;
	}
    }
    if(!tool) {
	printf("ERROR: Unknown config file\nAvailable options:");
	for(i = 0; cfgfile[i].name; i++)
	    printf(" %s", cfgfile[i].name);
	printf("\n");
	return 1;
    }

    printf("##\n## %s - automatically generated by clamconf "VERSION"\n##\n", name);
    printf("\n# Comment out or remove the line below.\nExample\n");

    for(i = 0; clam_options[i].owner; i++) {
	cpt = &clam_options[i];
	if(cpt->name && (cpt->owner & tool) && !(cpt->owner & OPT_DEPRECATED) && !(cpt->flags & 4)) {
	    strncpy(buffer, cpt->description, sizeof(buffer)-1);
	    buffer[sizeof(buffer)-1] = 0;
	    tokens_count = cli_strtokenize(buffer, '\n', 128, tokens);
	    printf("\n");
	    for(j = 0; j < tokens_count; j++)
		printf("# %s\n", tokens[j]);

	    switch(cpt->argtype) {
		case CLOPT_TYPE_STRING:
		    if(cpt->strarg)
			printf("# Default: %s\n", cpt->strarg);
		    else
			printf("# Default: disabled\n");
		    break;

		case CLOPT_TYPE_NUMBER:
		    if(cpt->numarg != -1)
			printf("# Default: %lld\n", cpt->numarg);
		    else
			printf("# Default: disabled\n");
		    break;

		case CLOPT_TYPE_SIZE:
		    printf("# You may use 'M' or 'm' for megabytes (1M = 1m = 1048576 bytes)\n# and 'K' or 'k' for kilobytes (1K = 1k = 1024 bytes). To specify the size\n# in bytes just don't use modifiers.\n");
		    if(cpt->numarg != -1)
			printf("# Default: %lld\n", cpt->numarg);
		    else
			printf("# Default: disabled\n");
		    break;

		case CLOPT_TYPE_BOOL:
		    if(cpt->numarg != -1)
			printf("# Default: %s\n", cpt->numarg ? "yes" : "no");
		    else
			printf("# Default: disabled\n");
		    break;

		default:
		    printf("!!! %s: UNKNOWN INTERNAL TYPE !!!\n", cpt->name);
	    }

	    if(cpt->suggested && strchr(cpt->suggested, '\n')) {
		strncpy(buffer, cpt->suggested, sizeof(buffer)-1);
		buffer[sizeof(buffer)-1] = 0;
		tokens_count = cli_strtokenize(buffer, '\n', 128, tokens);
		for(j = 0; j < tokens_count; j++)
		    printf("#%s %s\n", cpt->name, tokens[j]);
	    } else {
		printf("#%s %s\n", cpt->name, cpt->suggested ? cpt->suggested : "ARG");
	    }
	}
    }

    return 0;
}

static void help(void)
{
    printf("\n");
    printf("           Clam AntiVirus: Configuration Tool %s\n", get_version());
    printf("           By The ClamAV Team: http://www.clamav.net/about.html#credits\n");
    printf("           (C) 2009 Sourcefire, Inc.\n\n");

    printf("    --help                 -h         Show help\n");
    printf("    --version              -V         Show version\n");
    printf("    --config-dir=DIR       -c DIR     Read configuration files from DIR\n");
    printf("    --non-default          -n         Only display non-default settings\n");
    printf("    --generate-config=NAME -g NAME    Generate example config file\n");
    printf("\n");
    return;
}

static void print_platform(struct cli_environment *env)
{
    printf("\nPlatform information\n--------------------\n");
    printf("uname: %s %s %s %s\n",
	   env->sysname, env->release, env->version, env->machine);

    printf("OS: "TARGET_OS_TYPE", ARCH: "TARGET_ARCH_TYPE", CPU: "TARGET_CPU_TYPE"\n");

#ifdef C_LINUX
    if (!access("/usr/bin/lsb_release", X_OK)) {
	fputs("Full OS version: ", stdout);
	fflush(stdout);
	if (system("/usr/bin/lsb_release -d -s") == -1) {
	   perror("failed to determine");
	}
    }
#else
    /* e.g. Solaris */
    if (!access("/etc/release", R_OK)) {
        char buf[1024];
        FILE *f = fopen("/etc/release", "r");

        if (f) {
            fgets(buf, sizeof(buf), f);
            printf("Full OS version: %s", buf);
            fclose(f);
        }
    }
#endif

    if (strcmp(ZLIB_VERSION, zlibVersion()))
	printf("WARNING: zlib version mismatch: %s (%s)\n", ZLIB_VERSION, zlibVersion());
#ifdef ZLIB_VERNUM
    printf("zlib version: %s (%s), compile flags: %02lx\n",
	   ZLIB_VERSION, zlibVersion(), zlibCompileFlags());
#else
    /* old zlib w/o zlibCompileFlags() */
    printf("zlib version: %s (%s)\n",
	   ZLIB_VERSION, zlibVersion());
#endif

    if (env->triple[0])
    printf("Triple: %s\n", env->triple);
    if (env->cpu[0])
	printf("CPU: %s, %s\n", env->cpu, env->big_endian ? "Big-endian" : "Little-endian");
    printf("platform id: 0x%08x%08x%08x\n",
	   env->platform_id_a,
	   env->platform_id_b,
	   env->platform_id_c);
}

static void print_build(struct cli_environment *env)
{
    const char *name;
    const char *version = NULL;
    printf("\nBuild information\n-----------------\n");
    /* Try to print information about some commonly used compilers */
#ifdef __GNUC__
	version = __VERSION__;
#endif
    switch (env->compiler) {
	case compiler_gnuc:
	    name = "GNU C";
	    break;
	case compiler_clang:
	    name = "Clang";
	    break;
	case compiler_llvm:
	    name = "LLVM-GCC";
	    break;
	case compiler_intel:
	    name = "Intel Compiler";
	    break;
	case compiler_msc:
	    name = "Microsoft Visual C++";
	    break;
	case compiler_sun:
	    name = "Sun studio";
	    break;
	default:
	    name = NULL;
    }
    if (name)
	printf("%s: %s%s(%u.%u.%u)\n", name,
	       version ? version : "",
	       version ? " " : "",
	       env->c_version >> 16,
		   (env->c_version >> 8)&0xff,
		   (env->c_version)&0xff);
    cli_printcxxver();
#if defined(BUILD_CPPFLAGS) && defined(BUILD_CFLAGS) && defined(BUILD_CXXFLAGS) && defined(BUILD_LDFLAGS) && defined(BUILD_CONFIGURE_FLAGS)
    printf("CPPFLAGS: %s\nCFLAGS: %s\nCXXFLAGS: %s\nLDFLAGS: %s\nConfigure: %s\n",
	   BUILD_CPPFLAGS, BUILD_CFLAGS, BUILD_CXXFLAGS, BUILD_LDFLAGS,
	   BUILD_CONFIGURE_FLAGS);
#endif
    printf("sizeof(void*) = %d\n", env->sizeof_ptr);
    printf("Engine flevel: %d, dconf: %d\n",
	   env->functionality_level,
	   env->dconf_level);
}

static void print_dbs(const char *dir)
{
	DIR *dd;
	struct dirent *dent;
	char *dbfile;
	unsigned int flevel = cl_retflevel(), cnt, sigs = 0;
	struct cl_cvd *cvd;

    if((dd = opendir(dir)) == NULL) {
        printf("print_dbs: Can't open directory %s\n", dir);
        return;
    }

    while((dent = readdir(dd))) {
	if(dent->d_ino) {
	    if(CLI_DBEXT(dent->d_name)) {
		dbfile = (char *) malloc(strlen(dent->d_name) + strlen(dir) + 2);
		if(!dbfile) {
		    printf("print_dbs: Can't allocate memory for dbfile\n");
		    closedir(dd);
		    return;
		}
		sprintf(dbfile, "%s"PATHSEP"%s", dir, dent->d_name);
		if(cli_strbcasestr(dbfile, ".cvd") || cli_strbcasestr(dbfile, ".cld")) {
		    cvd = cl_cvdhead(dbfile);
		    if(!cvd) {
			printf("%s: Can't get information about the database\n", dbfile);
		    } else {
			const time_t t = cvd->stime;
			printf("%s: version %u, sigs: %u, built on %s", dent->d_name, cvd->version, cvd->sigs, ctime(&t));
			sigs += cvd->sigs;
			if(cvd->fl > flevel)
			    printf("%s: WARNING: This database requires f-level %u (current f-level: %u)\n", dent->d_name, cvd->fl, flevel);
			cl_cvdfree(cvd);
		    }
		} else if(cli_strbcasestr(dbfile, ".cbc")) {
		    printf("[3rd Party] %s: bytecode\n", dent->d_name);
		    sigs++;
		} else {
		    cnt = countlines(dbfile);
		    printf("[3rd Party] %s: %u sig%c\n", dent->d_name, cnt, cnt > 1 ? 's' : ' ');
		    sigs += cnt;
		}
		free(dbfile);
	    }
	}
    }
    closedir(dd);
    printf("Total number of signatures: %u\n", sigs);
}

int main(int argc, char **argv)
{
	const char *dir;
	char path[512], dbdir[512], clamd_dbdir[512], *pt;
	struct optstruct *opts, *toolopts;
	const struct optstruct *opt;
	unsigned int i, j;
	struct cli_environment env;

    opts = optparse(NULL, argc, argv, 1, OPT_CLAMCONF, 0, NULL);
    if(!opts) {
	printf("ERROR: Can't parse command line options\n");
	return 1;
    }

    if(optget(opts, "help")->enabled) {
	help();
	optfree(opts);
	return 0;
    }

    if(optget(opts, "version")->enabled) {
	printf("Clam AntiVirus Configuration Tool %s\n", get_version());
	optfree(opts);
	return 0;
    }

    if((opt = optget(opts, "generate-config"))->enabled) {
	printconf(opt->strarg);
	optfree(opts);
	return 0;
    }

    dbdir[0] = 0;
    clamd_dbdir[0] = 0;
    dir = optget(opts, "config-dir")->strarg;
    printf("Checking configuration files in %s\n", dir);
    for(i = 0; cfgfile[i].name; i++) {
	snprintf(path, sizeof(path), "%s"PATHSEP"%s", dir, cfgfile[i].name);
	path[511] = 0;
	if(access(path, R_OK)) {
	    printf("\n%s not found\n", cfgfile[i].name);
	    continue;
	}
	printf("\nConfig file: %s\n", cfgfile[i].name);
	for(j = 0; j < strlen(cfgfile[i].name) + 13; j++)
	    printf("-");
	printf("\n");
	toolopts = optparse(path, 0, NULL, 1, cfgfile[i].tool | OPT_DEPRECATED, 0, NULL);
	if(!toolopts)
	    continue;
	printopts(toolopts, optget(opts, "non-default")->enabled);
	if(cfgfile[i].tool == OPT_FRESHCLAM) {
	    opt = optget(toolopts, "DatabaseDirectory");
	    strncpy(dbdir, opt->strarg, sizeof(dbdir));
	    dbdir[sizeof(dbdir) - 1] = 0;
	} else if(cfgfile[i].tool == OPT_CLAMD) {
	    opt = optget(toolopts, "DatabaseDirectory");
	    strncpy(clamd_dbdir, opt->strarg, sizeof(clamd_dbdir));
	    clamd_dbdir[sizeof(clamd_dbdir) - 1] = 0;
	}
	optfree(toolopts);
    }
    optfree(opts);

    printf("\nSoftware settings\n-----------------\n");
    printf("Version: %s\n", cl_retver());
    if(strcmp(cl_retver(), get_version()))
	printf("WARNING: Version mismatch: libclamav=%s, clamconf=%s\n", cl_retver(), get_version());
    cl_init(CL_INIT_DEFAULT);
    printf("Optional features supported: ");
#ifdef USE_MPOOL
	printf("MEMPOOL ");
#endif
#ifdef SUPPORT_IPv6
	printf("IPv6 ");
#endif
#ifdef CLAMUKO
	printf("CLAMUKO ");
#endif
#ifdef C_BIGSTACK
	printf("BIGSTACK ");
#endif
#ifdef FRESHCLAM_DNS_FIX
	printf("FRESHCLAM_DNS_FIX ");
#endif
#ifndef _WIN32
        if (get_fpu_endian() != FPU_ENDIAN_UNKNOWN)
#endif
			printf("AUTOIT_EA06 ");
#ifdef HAVE_BZLIB_H
	printf("BZIP2 ");
#endif

#ifdef HAVE_LIBXML2
	printf("LIBXML2 ");
#endif
#ifdef HAVE_PCRE
	printf("PCRE ");
#endif
#ifdef HAVE_JSON
	printf("JSON ");
#endif
    if(have_rar)
	printf("RAR ");
    if (have_clamjit)
	printf("JIT");
    printf("\n");

    if(!strlen(dbdir)) {
	pt = freshdbdir();
	if(pt) {
	    strncpy(dbdir, pt, sizeof(dbdir));
	    free(pt);
	} else {
	    strncpy(dbdir, DATADIR, sizeof(dbdir));
	}
	dbdir[sizeof(dbdir) - 1] = 0;
    }

    printf("\nDatabase information\n--------------------\n");
    printf("Database directory: %s\n", dbdir);
    if(strcmp(dbdir, clamd_dbdir))
	printf("WARNING: freshclam.conf and clamd.conf point to different database directories\n");
    print_dbs(dbdir);

    cli_detect_environment(&env);
    print_platform(&env);
    print_build(&env);
    cl_cleanup_crypto();
    return 0;
}
