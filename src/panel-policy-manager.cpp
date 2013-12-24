/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2013, Regents of the University of California
 *                     Yingdi Yu
 *
 * BSD license, See the LICENSE file for more information
 *
 * Author: Yingdi Yu <yingdi@cs.ucla.edu>
 */

#include "panel-policy-manager.h"
#include "null-ptrs.h"
#include <ndn-cpp/sha256-with-rsa-signature.hpp>
#include <ndn-cpp/security/signature/sha256-with-rsa-handler.hpp>
// #include <boost/bind.hpp>

#include "logging.h"

using namespace std;
using namespace ndn;
using namespace ndn::ptr_lib;

INIT_LOGGER("PanelPolicyManager");

PanelPolicyManager::PanelPolicyManager(const int & stepLimit)
  : m_stepLimit(stepLimit)
  , m_certificateCache()
{
  m_localPrefixRegex = make_shared<Regex>("^<local><ndn><prefix><><>$");

  m_invitationDataSigningRule = make_shared<IdentityPolicyRule>("^<ndn><broadcast><chronos><invitation>([^<chatroom>]*)<chatroom>", 
                                                                "^([^<KEY>]*)<KEY>(<>*)<><ID-CERT><>$", 
                                                                "==", "\\1", "\\1\\2", true);
  
  m_dskRule = make_shared<IdentityPolicyRule>("^([^<KEY>]*)<KEY><dsk-.*><ID-CERT><>$", 
                                              "^([^<KEY>]*)<KEY>(<>*)<ksk-.*><ID-CERT>$", 
                                              "==", "\\1", "\\1\\2", true);
  
  m_endorseeRule = make_shared<IdentityPolicyRule>("^([^<DNS>]*)<DNS><>*<ENDORSEE><>$", 
                                                   "^([^<KEY>]*)<KEY>(<>*)<ksk-.*><ID-CERT>$", 
                                                   "==", "\\1", "\\1\\2", true);
  
  m_kskRegex = make_shared<Regex>("^([^<KEY>]*)<KEY>(<>*<ksk-.*>)<ID-CERT><>$", "\\1\\2");

  m_keyNameRegex = make_shared<Regex>("^([^<KEY>]*)<KEY>(<>*<ksk-.*>)<ID-CERT>$", "\\1\\2");

  m_signingCertificateRegex = make_shared<Regex>("^<ndn><broadcast><chronos><invitation>([^<chatroom>]*)<chatroom>", "\\1");
}

bool 
PanelPolicyManager::skipVerifyAndTrust (const Data & data)
{
  if(m_localPrefixRegex->match(data.getName()))
    return true;
  
  return false;
}

bool
PanelPolicyManager::requireVerify (const Data & data)
{
  // if(m_invitationDataRule->matchDataName(data))
  //   return true;
  if(m_kskRegex->match(data.getName()))
     return true;
  if(m_dskRule->matchDataName(data))
    return true;

  if(m_endorseeRule->matchDataName(data))
    return true;


  return false;
}

shared_ptr<ValidationRequest>
PanelPolicyManager::checkVerificationPolicy(const shared_ptr<Data>& data, 
                                            int stepCount, 
                                            const OnVerified& onVerified,
                                            const OnVerifyFailed& onVerifyFailed)
{
  if(m_stepLimit == stepCount)
    {
      _LOG_ERROR("Reach the maximum steps of verification!");
      onVerifyFailed(data);
      return CHRONOCHAT_NULL_VALIDATIONREQUEST_PTR;
    }

  const Sha256WithRsaSignature* sha256sig = dynamic_cast<const Sha256WithRsaSignature*>(data->getSignature());    

  if(ndn_KeyLocatorType_KEYNAME != sha256sig->getKeyLocator().getType())
    {
      _LOG_ERROR("Keylocator is not name!");
      onVerifyFailed(data);
      return CHRONOCHAT_NULL_VALIDATIONREQUEST_PTR;
    }

  const Name & keyLocatorName = sha256sig->getKeyLocator().getKeyName();

  if(m_kskRegex->match(data->getName()))
    {
      Name keyName = m_kskRegex->expand();
      map<Name, PublicKey>::iterator it = m_trustAnchors.find(keyName);
      if(m_trustAnchors.end() != it)
        {
          // _LOG_DEBUG("found key!");
          IdentityCertificate identityCertificate(*data);
          if(isSameKey(it->second.getKeyDer(), identityCertificate.getPublicKeyInfo().getKeyDer()))
            onVerified(data);
          else
            onVerifyFailed(data);
        }
      else
        onVerifyFailed(data);

      return CHRONOCHAT_NULL_VALIDATIONREQUEST_PTR;
    }

  if(m_dskRule->satisfy(*data))
    {
      m_keyNameRegex->match(keyLocatorName);
      Name keyName = m_keyNameRegex->expand();

      if(m_trustAnchors.end() != m_trustAnchors.find(keyName))
        if(Sha256WithRsaHandler::verifySignature(*data, m_trustAnchors[keyName]))
          onVerified(data);
        else
          onVerifyFailed(data);
      else
        onVerifyFailed(data);

      return CHRONOCHAT_NULL_VALIDATIONREQUEST_PTR;	
    }

  if(m_endorseeRule->satisfy(*data))
    {
      m_keyNameRegex->match(keyLocatorName);
      Name keyName = m_keyNameRegex->expand();
      if(m_trustAnchors.end() != m_trustAnchors.find(keyName))
        if(Sha256WithRsaHandler::verifySignature(*data, m_trustAnchors[keyName]))
          onVerified(data);
        else
          onVerifyFailed(data);
      else
        onVerifyFailed(data);

      return CHRONOCHAT_NULL_VALIDATIONREQUEST_PTR;
    }

  _LOG_DEBUG("Unverified!");

  onVerifyFailed(data);
  return CHRONOCHAT_NULL_VALIDATIONREQUEST_PTR;
}

bool 
PanelPolicyManager::checkSigningPolicy(const Name & dataName, const Name & certificateName)
{
  return m_invitationDataSigningRule->satisfy(dataName, certificateName);
}

Name 
PanelPolicyManager::inferSigningIdentity(const Name & dataName)
{
  if(m_signingCertificateRegex->match(dataName))
    return m_signingCertificateRegex->expand();
  else
    return Name();
}

void
PanelPolicyManager::addTrustAnchor(const EndorseCertificate& selfEndorseCertificate)
{ 
  // _LOG_DEBUG("Add Anchor: " << selfEndorseCertificate.getPublicKeyName().toUri());
  m_trustAnchors.insert(pair <Name, PublicKey > (selfEndorseCertificate.getPublicKeyName(), selfEndorseCertificate.getPublicKeyInfo())); 
}

void
PanelPolicyManager::removeTrustAnchor(const Name& keyName)
{  
  m_trustAnchors.erase(keyName); 
}

shared_ptr<PublicKey>
PanelPolicyManager::getTrustedKey(const Name& inviterCertName)
{
  Name keyLocatorName = inviterCertName.getPrefix(-1);
  m_keyNameRegex->match(keyLocatorName);
  Name keyName = m_keyNameRegex->expand();

  if(m_trustAnchors.end() != m_trustAnchors.find(keyName))
    return make_shared<PublicKey>(m_trustAnchors[keyName]);
  return CHRONOCHAT_NULL_PUBLICKEY_PTR;
}

bool
PanelPolicyManager::isSameKey(const Blob& keyA, const Blob& keyB)
{
  size_t size = keyA.size();

  if(size != keyB.size())
    return false;

  const uint8_t* ap = keyA.buf();
  const uint8_t* bp = keyB.buf();
  
  for(int i = 0; i < size; i++)
    {
      if(ap[i] != bp[i])
        return false;
    }

  return true;
}
