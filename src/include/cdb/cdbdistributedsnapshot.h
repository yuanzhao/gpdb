/*-------------------------------------------------------------------------
 *
 * cdbdistributedsnapshot.h
 *
 * Copyright (c) 2007-2008, Greenplum inc
 *
 *-------------------------------------------------------------------------
 */
#ifndef CDBDISTRIBUTEDSNAPSHOT_H
#define CDBDISTRIBUTEDSNAPSHOT_H

/* This is a shipped header, do not include other files under "cdb" */
#include "c.h"     /* DistributedTransactionId */

#define DistributedSnapshot_StaticInit {0,0,0,0,0,0}

typedef struct DistributedSnapshot
{
	DistributedTransactionTimeStamp		distribTransactionTimeStamp;
										/*
										 * The unique timestamp for this
										 * start of the DTM.  It applies to
										 * all of the distributed transactions
										 * in this snapshot.
										 */

	DistributedTransactionId		xminAllDistributedSnapshots;
										/*
										 * The lowest distributed transaction
										 * being used for distributed snapshots.
										 */
	
	DistributedSnapshotId			distribSnapshotId;
										/* 
										 * Unique number identifying this
										 * particular distributed snapshot.
										 */
										   
	DistributedTransactionId 	xmin;	/* XID < xmin are visible to me */
	DistributedTransactionId 	xmax;	/* XID >= xmax are invisible to me */
	int32						count;	/* 
										 * Count of distributed transactions
										 * in inProgress*Array. 
										 */
	int32						maxCount;
										/*
										 * Max entry count for 
										 * inProgress*Array.
										 */

	/* Array of distributed transactions in progress. */
	DistributedTransactionId        *inProgressXidArray;
} DistributedSnapshot;

/*
 * GPDB: Snapshot stores this information to check tuple visibility against
 * distributed transactions.
 */
typedef struct DistributedSnapshotWithLocalMapping
{
	DistributedSnapshot ds;

	/*
	 * Cache to perform quick check for localXid, populated after reverse
	 * mapping distributed xid to local xid.
	 */
	TransactionId minCachedLocalXid;
	TransactionId maxCachedLocalXid;
	int32 currentLocalXidsCount;
	int32 maxLocalXidsCount;
	TransactionId *inProgressMappedLocalXids;
} DistributedSnapshotWithLocalMapping;

typedef enum
{
	DISTRIBUTEDSNAPSHOT_COMMITTED_NONE = 0,		
	DISTRIBUTEDSNAPSHOT_COMMITTED_INPROGRESS,
	DISTRIBUTEDSNAPSHOT_COMMITTED_VISIBLE,
	DISTRIBUTEDSNAPSHOT_COMMITTED_IGNORE
} DistributedSnapshotCommitted;

extern bool
localXidSatisfiesAnyDistributedSnapshot(TransactionId localXid);

extern DistributedSnapshotCommitted DistributedSnapshotWithLocalMapping_CommittedTest(
	DistributedSnapshotWithLocalMapping		*dslm,
	TransactionId 							localXid,
	bool isVacuumCheck);

extern void DistributedSnapshot_Reset(
	DistributedSnapshot *distributedSnapshot);

extern void DistributedSnapshot_Copy(
	DistributedSnapshot *target,
	DistributedSnapshot *source);

extern int
DistributedSnapshot_SerializeSize(DistributedSnapshot *ds);

extern int
DistributedSnapshot_Serialize(DistributedSnapshot *ds, char *buf);

extern int
DistributedSnapshot_Deserialize(const char *buf, DistributedSnapshot *ds);

#endif   /* CDBDISTRIBUTEDSNAPSHOT_H */
