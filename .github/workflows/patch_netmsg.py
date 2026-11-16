# CS2 1.41.9.7 (2026-11-12) dropped INetworkMessageInternal: the engine now hands out a plain NetMessageInfo_t
# (no vtable, name and allocate function first), and INetworkMessages lost four virtuals, so FindNetworkMessagePartial
# and FindNetworkMessageById moved to other slots. hl2sdk 2283c1f9 has the new layout, but our SDK pin (and the pins of
# CSS 1.0.374 and MAM 1.5.4) can't move that far, so this rewrites the old headers in place:
# - NetMessageInfo_t gets the new layout;
# - INetworkMessageInternal becomes NetMessageInfo_t plus the old method names as non-virtual helpers, so existing
#   code (GetNetMessageInfo()->m_MessageId, AllocateMessage(), GetUnscopedName()) compiles and reads the new struct;
# - INetworkMessages gets the new virtual order.
# Usage: patch_netmsg.py <sdk root>. A no-op if already patched.
import re
import sys

root = sys.argv[1].rstrip("/\\")
MARK = "CS2 1.41.9.7 NetMessageInfo_t"


def patch(rel, replacements):
    path = root + "/" + rel
    src = open(path, encoding="utf-8", newline="").read()
    if MARK in src:
        print("already patched:", path)
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for pattern, text in replacements:
        new, n = re.subn(pattern, lambda m: text.replace("\n", nl), src, count=1, flags=re.S)
        assert n == 1, "%s not found once in %s" % (pattern, path)
        src = new
    open(path, "w", encoding="utf-8", newline="").write(src)
    print("patched:", path)


NETMSGINFO = """// CS2 1.41.9.7 NetMessageInfo_t (hl2sdk 2283c1f9): the engine passes this struct where it used to pass
// INetworkMessageInternal
struct NetMessageInfo_t
{
	CUtlString m_szName;
	CNetMessage *( *m_pfnAllocateMessage )();
	CUtlString m_szGroup;
	// int in the new SDK; the old NetworkMessageId typedef is uint16
	int m_MessageId;
	int m_nCategories;
	int m_nRequiredHandlerCategories;
	NetworkGroupId m_GroupId;
	// m_nBufType : 5, m_bCannotBeNetworked : 1, m_bOkayToRedispatch : 1
	uint8 m_nFlags;
};

// Same memory as NetMessageInfo_t; the old virtuals are plain helpers now
class INetworkMessageInternal : public NetMessageInfo_t
{
public:
	const char *GetUnscopedName() { return m_szName.Get(); }
	NetMessageInfo_t *GetNetMessageInfo() { return this; }
	CNetMessage *AllocateMessage() { return m_pfnAllocateMessage(); }
};"""

INETWORKMESSAGES = """// CS2 1.41.9.7 NetMessageInfo_t: virtual order from hl2sdk 2283c1f9, old types kept where callers need them
abstract_class INetworkMessages
{
public:
	virtual void RegisterNetworkCategory(NetworkCategoryId nCategoryId, char const *szDebugName) = 0;

	virtual void AssociateNetMessageWithChannelCategoryAbstract(INetworkMessageInternal *pNetMessage, NetworkCategoryId nCategoryId, bool bRequireHandler) = 0;

	virtual bool Serialize(bf_write &pBuf, const CNetMessage *pData) = 0;

	virtual bool UnserializeMessageInternal( bf_read &pBuf, CNetMessage *pData, CUtlString *err_reason ) = 0;
	virtual bool SerializeMessageInternal( bf_write &pBuf, const CNetMessage *pData ) = 0;

	virtual CNetMessage *UnserializeFromStream(bf_read &pBuf, CUtlString &err_reason) = 0;

	virtual void *RegisterNetworkFieldSerializer(char const *, NetworkSerializationMode_t, NetworkableDataType_t, int, NetworkFieldSerializeCB, NetworkFieldUnserializeCB, NetworkFieldInfoCB,
		NetworkFieldMetaInfoCB, NetworkableDataCB, NetworkUnkCB001, NetworkFieldSerializeCB, NetworkFieldUnserializeCB) = 0;
	virtual void *RegisterNetworkArrayFieldSerializer(char const *, NetworkSerializationMode_t, NetworkFieldSerializeBufferCB, NetworkFieldUnserializeBufferCB, NetworkFieldInfoCB,
		NetworkFieldMetaInfoCB, NetworkFieldSerializeBufferCB, NetworkFieldUnserializeBufferCB) = 0;

	virtual NetMessageInfo_t *GetNetMessageInfo(INetworkMessageInternal *pNetMessage) = 0;

	virtual INetworkMessageInternal* FindNetworkMessage(char const *szName) = 0;
	virtual INetworkMessageInternal* FindNetworkMessagePartial(char const *szPartialName) = 0;

	virtual NetworkGroupId FindNetworkGroup(char const *szGroup, bool bCreateIfNotFound = false) = 0;
	virtual int GetNetworkGroupCount() = 0;
	virtual const char *GetNetworkGroupName(NetworkGroupId nGroupId) = 0;
	virtual Color GetNetworkGroupColor(NetworkGroupId nGroupId) = 0;

	virtual void AssociateNetMessageGroupIdWithChannelCategory(NetworkCategoryId nCategoryId, char const *szGroup) = 0;

	virtual void SetNetworkSerializationContextData(char const *szContext, NetworkSerializationMode_t, void *) = 0;
	virtual uint16 LookupNetworkSerializationContextDataId(char const *szContext) = 0;
	virtual void *GetNetworkSerializationContextData(uint16 hContext, NetworkSerializationMode_t mode) = 0;

	virtual void RegisterNetworkChangeTagCallback(const void *pCallback) = 0;

	virtual INetworkMessageInternal *FindOrCreateNetMessage(int nMessageId, CNetMessage *( *pfnAllocateMessage )(), int nGroup, int nDefaultBufferType, bool bOkayToRedispatch, bool bCannotBeNetworked) = 0;

	virtual void RegisterNetworkFieldChangeCallbackInternal(char const *szCallbackName, char const *szClassName, NetworkFieldChangedDelegateType_t fieldType, CUtlAbstractDelegate pCallback, NetworkFieldChangeCallbackPerformType_t cbPerformType, int nPriority ) = 0;

	virtual void AllowAdditionalMessageRegistration(bool bAllow) = 0;
	virtual bool IsAdditionalMessageRegistrationAllowed() = 0;

	virtual int GetFieldChangeCallbackOrderCount() = 0;
	virtual void GetFieldChangeCallbackPriorities(int nCount, int *nPriorities) = 0;

	virtual void RegisterFieldChangeCallbackPriority(int nPriority) = 0;

	virtual INetworkMessageInternal *FindNetworkMessageById(int nMessageId) = 0;

	virtual void SetIsForServer(bool bIsForServer) = 0;
	virtual bool GetIsForServer() = 0;

	virtual void RegisterSchemaTypeOverride(uint, char const *) = 0;

	virtual int ComputeOrderForPriority(int nPriority) = 0;

	virtual CLoggingSystem::LoggingChannel_t *GetLoggingChannel() = 0;

	virtual void SetCoordSizeParams(const void *pParams) = 0;
	virtual const void *GetCoordSizeParams() = 0;

	virtual ~INetworkMessages() = 0;
};"""

patch("public/networksystem/inetworkserializer.h", [
    (r"struct NetMessageInfo_t\s*\{.*?\n\};\s*abstract_class INetworkMessageInternal\s*\{.*?\n\};", NETMSGINFO),
])
patch("public/networksystem/inetworkmessages.h", [
    (r"abstract_class INetworkMessages\s*\{.*?\n\};", INETWORKMESSAGES),
])
