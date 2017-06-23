/*-------------------------------------------------------------------------
 *
 * resgroup.h
 *	  GPDB resource group definitions.
 *
 *
 * Copyright (c) 2006-2017, Greenplum inc.
 *
 *-------------------------------------------------------------------------
 */
#ifndef RES_GROUP_H
#define RES_GROUP_H

/*
 * GUC variables.
 */
extern int MaxResourceGroups;

/*
 * Data structures
 */
/* Resource Groups */
typedef struct ResGroupData
{
	Oid			groupId;		/* Id for this group */
	int 		nRunning;		/* number of running trans */
	PROC_QUEUE	waitProcs;
	int			totalExecuted;	/* total number of executed trans */
	int			totalQueued;	/* total number of queued trans	*/
	Interval	totalQueuedTime;/* total queue time */

	bool		lockedForDrop;  /* true if resource group is dropped but not committed yet */

	/*
	 * memory usage of this group, should always equal to the
	 * sum of session memory(session_state->sessionVmem) that
	 * belongs to this group
	 */
	uint32		totalMemoryUsage;
} ResGroupData;
typedef ResGroupData *ResGroup;

/*
 * The hash table for resource groups in shared memory should only be populated
 * once, so we add a flag here to implement this requirement.
 */
typedef struct ResGroupControl
{
	HTAB			*htbl;
	bool			loaded;
} ResGroupControl;

/* Type of statistic infomation */
typedef enum
{
	RES_GROUP_STAT_UNKNOWN = -1,

	RES_GROUP_STAT_NRUNNING = 0,
	RES_GROUP_STAT_NQUEUEING,
	RES_GROUP_STAT_TOTAL_EXECUTED,
	RES_GROUP_STAT_TOTAL_QUEUED,
	RES_GROUP_STAT_TOTAL_QUEUE_TIME,
	RES_GROUP_STAT_CPU_USAGE,
	RES_GROUP_STAT_MEM_USAGE,
} ResGroupStatType;

/*
 * Functions in resgroup.c
 */
/* Shared memory and semaphores */
extern Size ResGroupShmemSize(void);
extern void ResGroupControlInit(void);

/* Load resource group information from catalog */
extern void	InitResGroups(void);

extern void AllocResGroupEntry(Oid groupId);
extern void FreeResGroupEntry(Oid groupId);

/* Acquire and release resource group slot */
extern void ResGroupSlotAcquire(void);
extern void ResGroupSlotRelease(void);

/* Assign current process to the associated resource group */
extern void AssignResGroup(Oid groupId);
extern void SetCurrentResGroup(void);
extern void ResetCurrentResGroup(void);

/* Retrieve statistic information of type from resource group */
extern void ResGroupGetStat(Oid groupId, ResGroupStatType type, char *retStr, int retStrLen, const char *prop);

/* Update the memory usage of resource group */
extern void ResGroupUpdateMemoryUsage(int32 memoryChunks);

extern void ResGroupAlterCheckForWakeup(Oid groupId, int value, int proposed);
extern void ResGroupDropCheckForWakeup(Oid groupId, bool isCommit);
extern void ResGroupCheckForDrop(Oid groupId, char *name);
extern int CalcConcurrencyValue(int groupId, int val, int proposed, int newProposed);

#define LOG_RESGROUP_DEBUG(...) \
	do {if (Debug_resource_group) elog(__VA_ARGS__); } while(false);

#endif   /* RES_GROUP_H */
