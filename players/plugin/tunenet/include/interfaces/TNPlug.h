#ifndef TNPLUG_INTERFACE_DEF_H
#define TNPLUG_INTERFACE_DEF_H

/*
** This file was machine generated by idltool 51.6.
** Do not edit
*/ 

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_EXEC_H
#include <exec/exec.h>
#endif
#ifndef EXEC_INTERFACES_H
#include <exec/interfaces.h>
#endif

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

struct TNPlugIFace
{
	struct InterfaceData Data;

	ULONG APICALL (*Obtain)(struct TNPlugIFace *Self);
	ULONG APICALL (*Release)(struct TNPlugIFace *Self);
	void APICALL (*Expunge)(struct TNPlugIFace *Self);
	struct Interface * APICALL (*Clone)(struct TNPlugIFace *Self);
	struct audio_player * APICALL (*AnnouncePlayer)(struct TNPlugIFace *Self, ULONG version);
	BOOL APICALL (*InitPlayer)(struct TNPlugIFace *Self, struct TuneNet * inTuneNet, ULONG playmode);
	BOOL APICALL (*OpenPlayer)(struct TNPlugIFace *Self, UBYTE * openme, struct TuneNet * inTuneNet);
	VOID APICALL (*ClosePlayer)(struct TNPlugIFace *Self, struct TuneNet * inTuneNet);
	LONG APICALL (*DecodeFramePlayer)(struct TNPlugIFace *Self, struct TuneNet * inTuneNet, WORD * outbuffer);
	VOID APICALL (*ExitPlayer)(struct TNPlugIFace *Self, struct audio_player * aplayer);
	LONG APICALL (*TestPlayer)(struct TNPlugIFace *Self, UBYTE * testme, UBYTE * testbuffer, ULONG totalsize, ULONG testsize);
	LONG APICALL (*SeekPlayer)(struct TNPlugIFace *Self, struct TuneNet * inTuneNet, ULONG seconds);
	LONG APICALL (*DoNotify)(struct TNPlugIFace *Self, struct TuneNet * inTuneNet, ULONG item, ULONG value);
};

#endif /* TNPLUG_INTERFACE_DEF_H */
