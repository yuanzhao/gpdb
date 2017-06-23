/*-------------------------------------------------------------------------
 *
 * resgroup-ops-cgroup.c
 *	  OS dependent resource group operations - cgroup implementation
 *
 *
 * Copyright (c) 2017, Pivotal Software Inc.
 *
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "cdb/cdbvars.h"
#include "postmaster/backoff.h"
#include "utils/resgroup-ops.h"

#ifndef __linux__
#error  cgroup is only available on linux
#endif

#include <unistd.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/stat.h>

/*
 * Interfaces for OS dependent operations.
 *
 * Resource group relies on OS dependent group implementation to manage
 * resources like cpu usage, such as cgroup on Linux system.
 * We call it OS group in below function description.
 *
 * So far these operations are mainly for CPU rate limitation and accounting.
 */

#define CGROUP_ERROR_PREFIX "cgroup is not properly configured: "
#define CGROUP_ERROR(...) do { \
	elog(ERROR, CGROUP_ERROR_PREFIX __VA_ARGS__); \
} while (false)

static char * buildPath(Oid group, const char *comp, const char *prop, char *path, size_t pathsize);
static int lockDir(const char *path, bool block);
static void unassignGroup(Oid group, const char *comp, int fddir);
static bool createDir(Oid group, const char *comp);
static bool removeDir(Oid group, const char *comp, bool unassign);
static int getCpuCores(void);
static size_t readData(Oid group, const char *comp, const char *prop, char *data, size_t datasize);
static void writeData(Oid group, const char *comp, const char *prop, char *data, size_t datasize);
static int64 readInt64(Oid group, const char *comp, const char *prop);
static void writeInt64(Oid group, const char *comp, const char *prop, int64 x);
static bool checkPermission(Oid group, bool report);

static int cpucores = 0;

/*
 * Build path string with parameters.
 *
 * - if group is 0 then the path is for the gpdb toplevel cgroup;
 * - if prop is "" then the path is for the cgroup dir;
 */
static char *
buildPath(Oid group,
		  const char *comp,
		  const char *prop,
		  char *path,
		  size_t pathsize)
{
	if (group)
		snprintf(path, pathsize, "/sys/fs/cgroup/%s/gpdb/%d/%s", comp, group, prop);
	else
		snprintf(path, pathsize, "/sys/fs/cgroup/%s/gpdb/%s", comp, prop);

	return path;
}

/*
 * Unassign all the processes from group.
 *
 * These processes will be moved to the gpdb toplevel cgroup.
 *
 * This function must be called with the gpdb toplevel dir locked,
 * fddir is the fd for this lock, on any failure fddir will be closed
 * (and unlocked implicitly) then an error is raised.
 */
static void
unassignGroup(Oid group, const char *comp, int fddir)
{
	char path[128];
	size_t pathsize = sizeof(path);
	char *buf;
	size_t bufsize;
	const size_t bufdeltasize = 512;
	size_t buflen = -1;
	int fdr = -1;
	int fdw = -1;

	/*
	 * Check an operation result on path.
	 *
	 * Operation can be open(), close(), read(), write(), etc., which must
	 * set the errno on error.
	 *
	 * - condition describes the expected result of the operation;
	 * - action is the cleanup action on failure, such as closing the fd,
	 *   multiple actions can be specified by putting them in brackets,
	 *   such as (op1, op2);
	 * - message describes what's failed;
	 */
#define __CHECK(condition, action, message) do { \
	if (!(condition)) \
	{ \
		/* save errno in case it's changed in actions */ \
		int err = errno; \
		action; \
		CGROUP_ERROR(message ": %s: %s", path, strerror(err)); \
	} \
} while (0)

	buildPath(group, comp, "cgroup.procs", path, pathsize);

	fdr = open(path, O_RDONLY);
	__CHECK(fdr >= 0, ( close(fddir) ), "can't open file for read");

	buflen = 0;
	bufsize = bufdeltasize;
	buf = palloc(bufsize);

	while (1)
	{
		int n = read(fdr, buf + buflen, bufdeltasize);
		__CHECK(n >= 0, ( close(fdr), close(fddir) ), "can't read from file");

		buflen += n;

		if (n < bufdeltasize)
			break;

		bufsize += bufdeltasize;
		buf = repalloc(buf, bufsize);
	}

	close(fdr);

	buildPath(0, comp, "cgroup.procs", path, pathsize);

	fdw = open(path, O_WRONLY);
	__CHECK(fdw >= 0, ( close(fddir) ), "can't open file for write");

	char *ptr = buf;
	char *end = NULL;
	long pid;

	/*
	 * as required by cgroup, only one pid can be migrated in each single
	 * write() call, so we have to parse the pids from the buffer first,
	 * then write them one by one.
	 */
	while (1)
	{
		pid = strtol(ptr, &end, 10);
		if (ptr == end)
			break;

		int n = write(fdw, ptr, end - ptr);
		__CHECK(n >= 0, ( close(fddir) ), "can't write to file");
		__CHECK(n == end - ptr, ( close(fddir) ), "can't write to file");

		ptr = end;
	}

	close(fdw);

#undef __CHECK
}

/*
 * Lock the dir specified by path.
 *
 * - path must be a dir path;
 * - if block is true then lock in block mode, otherwise will give up if
 *   the dir is already locked;
 */
static int
lockDir(const char *path, bool block)
{
	int fddir;

	fddir = open(path, O_RDONLY | O_DIRECTORY);
	if (fddir < 0)
	{
		if (errno == ENOENT)
		{
			/* the dir doesn't exist, nothing to do */
			return -1;
		}

		CGROUP_ERROR("can't open dir to lock: %s: %s",
					 path, strerror(errno));
	}

	int flags = LOCK_EX;
	if (block)
		flags |= LOCK_NB;

	while (flock(fddir, flags))
	{
		/*
		 * EAGAIN is not described in flock(2),
		 * however it does appear in practice.
		 */
		if (errno == EAGAIN)
			continue;

		int err = errno;
		close(fddir);

		/*
		 * In block mode all errors should be reported;
		 * In non block mode only report errors != EWOULDBLOCK.
		 */
		if (block || err != EWOULDBLOCK)
			CGROUP_ERROR("can't lock dir: %s: %s", path, strerror(err));
		return -1;
	}

	/*
	 * Even if we accquired the lock the dir may still been removed by other
	 * processes, e.g.:
	 *
	 * 1: open()
	 * 1: flock() -- process 1 accquired the lock
	 *
	 * 2: open()
	 * 2: flock() -- blocked by process 1
	 *
	 * 1: rmdir()
	 * 1: close() -- process 1 released the lock
	 *
	 * 2:flock() will now return w/o error as process 2 still has a valid
	 * fd (reference) on the target dir, and process 2 does accquired the lock
	 * successfully. However as the dir is already removed so process 2
	 * shouldn't make any further operation (rmdir(), etc.) on the dir.
	 *
	 * So we check for the existence of the dir again and give up if it's
	 * already removed.
	 */
	if (access(path, F_OK))
	{
		/* the dir is already removed by other process, nothing to do */
		close(fddir);
		return -1;
	}

	return fddir;
}

/*
 * Create the cgroup dir for group.
 */
static bool
createDir(Oid group, const char *comp)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, comp, "", path, pathsize);

	if (mkdir(path, 0755) && errno != EEXIST)
		return false;

	return true;
}

/*
 * Remove the cgroup dir for group.
 *
 * - if unassign is true then unassign all the processes first before removal;
 */
static bool
removeDir(Oid group, const char *comp, bool unassign)
{
	char path[128];
	size_t pathsize = sizeof(path);
	int fddir;

	buildPath(group, comp, "", path, pathsize);

	/*
	 * To prevent race condition between multiple processes we require a dir
	 * to be removed with the lock accquired first.
	 */
	fddir = lockDir(path, true);
	if (fddir < 0)
	{
		/* the dir is already removed */
		return true;
	}

	if (unassign)
		unassignGroup(group, comp, fddir);

	if (rmdir(path))
	{
		int err = errno;

		close(fddir);

		/*
		 * we don't check for ENOENT again as we already accquired the lock
		 * on this dir and the dir still exist at that time, so if then
		 * it's removed by other processes then it's a bug.
		 */
		CGROUP_ERROR("can't remove dir: %s: %s", path, strerror(err));
	}

	/* close() also releases the lock */
	close(fddir);

	return true;
}

/*
 * Get the cpu cores assigned for current system or container.
 *
 * Suppose a physical machine has 8 cpu cores, 2 of them assigned to
 * a container, then the return value is:
 * - 8 if running directly on the machine;
 * - 2 if running in the container;
 */
static int
getCpuCores(void)
{
	if (cpucores == 0)
	{
		/*
		 * cpuset ops requires _GNU_SOURCE to be defined,
		 * and _GNU_SOURCE is forced on in src/template/linux,
		 * so we assume these ops are always available on linux.
		 */
		cpu_set_t cpuset;
		int i;

		if (sched_getaffinity(0, sizeof(cpuset), &cpuset) < 0)
			CGROUP_ERROR("can't get cpu cores: %s", strerror(errno));

		for (i = 0; i < CPU_SETSIZE; i++)
		{
			if (CPU_ISSET(i, &cpuset))
				cpucores++;
		}
	}

	if (cpucores == 0)
		CGROUP_ERROR("can't get cpu cores");

	return cpucores;
}

/*
 * Read at most datasize bytes from a cgroup interface file.
 */
static size_t
readData(Oid group, const char *comp, const char *prop, char *data, size_t datasize)
{
	char path[128];
	size_t pathsize = sizeof(path);

	buildPath(group, comp, prop, path, pathsize);

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		CGROUP_ERROR("can't open file '%s': %s", path, strerror(errno));

	ssize_t ret = read(fd, data, datasize);

	/* save errno before close() */
	int err = errno;
	close(fd);

	if (ret < 0)
		CGROUP_ERROR("can't read data from file '%s': %s", path, strerror(err));

	return ret;
}

/*
 * Write datasize bytes to a cgroup interface file.
 */
static void
writeData(Oid group, const char *comp, const char *prop, char *data, size_t datasize)
{
	char path[128];
	size_t pathsize = sizeof(path);

	buildPath(group, comp, prop, path, pathsize);

	int fd = open(path, O_WRONLY);
	if (fd < 0)
		CGROUP_ERROR("can't open file '%s': %s", path, strerror(errno));

	ssize_t ret = write(fd, data, datasize);

	/* save errno before close */
	int err = errno;
	close(fd);

	if (ret < 0)
		CGROUP_ERROR("can't write data to file '%s': %s", path, strerror(err));
	if (ret != datasize)
		CGROUP_ERROR("can't write all data to file '%s'", path);
}

/*
 * Read an int64 value from a cgroup interface file.
 */
static int64
readInt64(Oid group, const char *comp, const char *prop)
{
	int64 x;
	char data[64];
	size_t datasize = sizeof(data);

	readData(group, comp, prop, data, datasize);

	if (sscanf(data, "%lld", (long long *) &x) != 1)
		CGROUP_ERROR("invalid number '%s'", data);

	return x;
}

/*
 * Write an int64 value to a cgroup interface file.
 */
static void
writeInt64(Oid group, const char *comp, const char *prop, int64 x)
{
	char data[64];
	size_t datasize = sizeof(data);

	snprintf(data, datasize, "%lld", (long long) x);

	writeData(group, comp, prop, data, strlen(data));
}

/*
 * Check permissions on group's cgroup dir & interface files.
 *
 * - if report is true then raise an error on and bad permission,
 *   otherwise only return false;
 */
static bool
checkPermission(Oid group, bool report)
{
	char path[128];
	size_t pathsize = sizeof(path);
	const char *comp;

#define __CHECK(prop, perm) do { \
	buildPath(group, comp, prop, path, pathsize); \
	if (access(path, perm)) \
	{ \
		if (report) \
		{ \
			CGROUP_ERROR("can't access %s '%s': %s", \
						 prop[0] ? "file" : "directory", \
						 path, \
						 strerror(errno)); \
		} \
		return false; \
	} \
} while (0)

    /*
     * These checks should keep in sync with
     * gpMgmt/bin/gpcheckresgroupimpl
     */

	comp = "cpu";

	__CHECK("", R_OK | W_OK | X_OK);
	__CHECK("cgroup.procs", R_OK | W_OK);
	__CHECK("cpu.cfs_period_us", R_OK | W_OK);
	__CHECK("cpu.cfs_quota_us", R_OK | W_OK);
	__CHECK("cpu.shares", R_OK | W_OK);

	comp = "cpuacct";

	__CHECK("", R_OK | W_OK | X_OK);
	__CHECK("cgroup.procs", R_OK | W_OK);
	__CHECK("cpuacct.usage", R_OK);
	__CHECK("cpuacct.stat", R_OK);

#undef __CHECK

	return true;
}

/* Return the name for the OS group implementation */
const char *
ResGroupOps_Name(void)
{
	return "cgroup";
}

/* Check whether the OS group implementation is available and useable */
void
ResGroupOps_Bless(void)
{
	checkPermission(0, true);
}

/* Initialize the OS group */
void
ResGroupOps_Init(void)
{
	/* cfs_quota_us := cfs_period_us * ncores * gp_resource_group_cpu_limit */
	/* shares := 1024 * 256 (max possible value) */

	int64 cfs_period_us;
	int ncores = getCpuCores();
	const char *comp = "cpu";

	cfs_period_us = readInt64(0, comp, "cpu.cfs_period_us");
	writeInt64(0, comp, "cpu.cfs_quota_us",
			   cfs_period_us * ncores * gp_resource_group_cpu_limit);
	writeInt64(0, comp, "cpu.shares", 1024 * 256);
}

/* Adjust GUCs for this OS group implementation */
void
ResGroupOps_AdjustGUCs(void)
{
	/*
	 * cgroup cpu limitation works best when all processes have equal
	 * priorities, so we force all the segments and postmaster to
	 * work with nice=0.
	 *
	 * this function should be called before GUCs are dispatched to segments.
	 */
	/* TODO: when cgroup is enabled we should move postmaster and maybe
	 *       also other processes to a separate group or gpdb toplevel */
	if (gp_segworker_relative_priority != 0)
	{
		/* TODO: produce a warning */
		gp_segworker_relative_priority = 0;
	}
}

/*
 * Create the OS group for group.
 */
void
ResGroupOps_CreateGroup(Oid group)
{
	int retry = 0;

	if (!createDir(group, "cpu") || !createDir(group, "cpuacct"))
	{
		CGROUP_ERROR("can't create cgroup for resgroup '%d': %s",
					 group, strerror(errno));
	}

	/*
	 * although the group dir is created the interface files may not be
	 * created yet, so we check them repeatedly until everything is ready.
	 */
	while (++retry <= 10 && !checkPermission(group, false))
		pg_usleep(1000);

	if (retry > 10)
	{
		/*
		 * still not ready after 10 retries, might be a real error,
		 * raise the error.
		 */
		checkPermission(group, true);
	}
}

/*
 * Destroy the OS group for group.
 *
 * Fail if any process is running under it.
 */
void
ResGroupOps_DestroyGroup(Oid group)
{
	if (!removeDir(group, "cpu", true) || !removeDir(group, "cpuacct", true))
	{
		CGROUP_ERROR("can't remove cgroup for resgroup '%d': %s",
			 group, strerror(errno));
	}
}

/*
 * Assign a process to the OS group. A process can only be assigned to one
 * OS group, if it's already running under other OS group then it'll be moved
 * out that OS group.
 *
 * pid is the process id.
 */
void
ResGroupOps_AssignGroup(Oid group, int pid)
{
	writeInt64(group, "cpu", "cgroup.procs", pid);
	writeInt64(group, "cpuacct", "cgroup.procs", pid);
}

/*
 * Set the cpu rate limit for the OS group.
 *
 * cpu_rate_limit should be within (0.0, 1.0].
 */
void
ResGroupOps_SetCpuRateLimit(Oid group, float cpu_rate_limit)
{
	const char *comp = "cpu";

	/* SUB/shares := TOP/shares * cpu_rate_limit */

	int64 shares = readInt64(0, comp, "cpu.shares");
	writeInt64(group, comp, "cpu.shares", shares * cpu_rate_limit);
}

/*
 * Get the cpu usage of the OS group, that is the total cpu time obtained
 * by this OS group, in nano seconds.
 */
int64
ResGroupOps_GetCpuUsage(Oid group)
{
	const char *comp = "cpuacct";

	return readInt64(group, comp, "cpuacct.usage");
}

/*
 * Get the count of cpu cores on the system.
 */
int
ResGroupOps_GetCpuCores(void)
{
	return getCpuCores();
}
