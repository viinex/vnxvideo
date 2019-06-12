#ifdef _WIN32

#include <system_error>

#include "Win32Utils.h"

std::shared_ptr<ACL> BuildDacl777() {
    PSID psid = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
    if (!AllocateAndInitializeSid(&SIDAuthWorld, 1,
        SECURITY_WORLD_RID,
        0, 0, 0, 0, 0, 0, 0,
        &psid))
    {
        DWORD err = GetLastError();
        throw std::system_error(err, std::system_category());
    }
    std::shared_ptr<SID> pEveryoneSID((SID*)psid, FreeSid);

    DWORD dwAclSize = sizeof(ACL) +
        2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
        GetLengthSid(pEveryoneSID.get());

    std::shared_ptr<ACL> pACL((PACL)malloc(dwAclSize), [pEveryoneSID](ACL* p) {free(p); });

    InitializeAcl(pACL.get(), dwAclSize, ACL_REVISION);

    if (!AddAccessAllowedAce(pACL.get(),
        ACL_REVISION,
        GENERIC_ALL,
        pEveryoneSID.get()
    ))
    {
        DWORD err = GetLastError();
        throw std::system_error(err, std::system_category());
    }
    return pACL;
}

std::shared_ptr<SECURITY_ATTRIBUTES> BuildSecurityAttributes777() {
    std::shared_ptr<SECURITY_DESCRIPTOR> pSD((SECURITY_DESCRIPTOR*)malloc(SECURITY_DESCRIPTOR_MIN_LENGTH), free);
    InitializeSecurityDescriptor(pSD.get(), SECURITY_DESCRIPTOR_REVISION);

    std::shared_ptr<ACL> pACL(BuildDacl777());
    DWORD dwRes = SetSecurityDescriptorDacl(pSD.get(), TRUE, pACL.get(), FALSE);

    std::shared_ptr<SECURITY_ATTRIBUTES> psa((SECURITY_ATTRIBUTES*)malloc(sizeof SECURITY_ATTRIBUTES),
        [pSD, pACL](SECURITY_ATTRIBUTES* p) { free(p); });
    psa->nLength = sizeof(SECURITY_ATTRIBUTES);
    psa->bInheritHandle = FALSE;
    psa->lpSecurityDescriptor = pSD.get();

    return psa;
}

#endif
