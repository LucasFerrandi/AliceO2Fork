// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
#include "Framework/DataAllocator.h"
#include "Framework/MessageContext.h"
#include "Framework/RootObjectContext.h"
#include "Framework/DataSpecUtils.h"
#include "Framework/DataProcessingHeader.h"
#include <TClonesArray.h>

namespace o2 {
namespace framework {

using DataHeader= o2::Header::DataHeader;
using DataProcessingHeader= o2::framework::DataProcessingHeader;

DataAllocator::DataAllocator(FairMQDevice *device,
                             MessageContext *context,
                             RootObjectContext *rootContext,
                             const AllowedOutputsMap &outputs)
: mDevice{device},
  mContext{context},
  mRootContext{rootContext},
  mAllowedOutputs{outputs}
{
}

std::string
DataAllocator::matchDataHeader(const OutputSpec &spec, size_t timeslice) {
  // FIXME: we should take timeframeId into account as well.
  for (auto &output : mAllowedOutputs) {
    if (DataSpecUtils::match(output.matcher, spec.origin, spec.description, spec.subSpec)
        && ((timeslice % output.maxTimeslices) == output.timeslice)) {
      return output.channel;
    }
  }
  std::ostringstream str;
  str << "Worker is not authorised to create message with "
      << "origin(" << spec.origin.str << ")"
      << "description(" << spec.description.str << ")"
      << "subSpec(" << spec.subSpec << ")";
  throw std::runtime_error(str.str());
}

DataChunk
DataAllocator::newChunk(const OutputSpec &spec, size_t size) {
  std::string channel = matchDataHeader(spec, mContext->timeslice());

  DataHeader dh;
  dh.dataOrigin = spec.origin;
  dh.dataDescription = spec.description;
  dh.subSpecification = spec.subSpec;

  DataProcessingHeader dph{mContext->timeslice(), 1};
  //we have to move the incoming data
  o2::Header::Stack headerStack{dh, dph};
  FairMQMessagePtr headerMessage = mDevice->NewMessageFor(channel, 0,
                                                          headerStack.buffer.get(),
                                                          headerStack.bufferSize,
                                                          &o2::Header::Stack::freefn,
                                                          headerStack.buffer.get());
  headerStack.buffer.release();
  FairMQMessagePtr payloadMessage = mDevice->NewMessageFor(channel, 0, size);
  auto dataPtr = payloadMessage->GetData();
  auto dataSize = payloadMessage->GetSize();

  FairMQParts parts;
  parts.AddPart(std::move(headerMessage));
  parts.AddPart(std::move(payloadMessage));
  assert(parts.Size() == 2);
  mContext->addPart(std::move(parts), channel);
  assert(parts.Size() == 0);
  return DataChunk{reinterpret_cast<char*>(dataPtr), dataSize};
}

DataChunk
DataAllocator::adoptChunk(const OutputSpec &spec, char *buffer, size_t size, fairmq_free_fn *freefn, void *hint = nullptr) {
  // Find a matching channel, create a new message for it and put it in the
  // queue to be sent at the end of the processing
  std::string channel = matchDataHeader(spec, mContext->timeslice());

  DataHeader dh;
  dh.dataOrigin = spec.origin;
  dh.dataDescription = spec.description;
  dh.subSpecification = spec.subSpec;

  DataProcessingHeader dph{mContext->timeslice(), 1};
  //we have to move the incoming data
  o2::Header::Stack headerStack{dh, dph};
  FairMQMessagePtr headerMessage = mDevice->NewMessageFor(channel, 0,
                                                          headerStack.buffer.get(),
                                                          headerStack.bufferSize,
                                                          &o2::Header::Stack::freefn,
                                                          headerStack.buffer.get());
  headerStack.buffer.release();

  FairMQParts parts;

  // FIXME: how do we want to use subchannels? time based parallelism?
  FairMQMessagePtr payloadMessage = mDevice->NewMessageFor(channel, 0, buffer, size, freefn, hint);
  auto dataPtr = payloadMessage->GetData();
  LOG(DEBUG) << "New payload at " << payloadMessage->GetData();
  auto dataSize = payloadMessage->GetSize();
  parts.AddPart(std::move(headerMessage));
  parts.AddPart(std::move(payloadMessage));
  mContext->addPart(std::move(parts), channel);
  return DataChunk{reinterpret_cast<char *>(dataPtr), dataSize};
}

TClonesArray&
DataAllocator::newTClonesArray(const OutputSpec &spec, const char *className, size_t nElements) {
  std::string channel = matchDataHeader(spec, mRootContext->timeslice());

  DataHeader dh;
  dh.dataOrigin = spec.origin;
  dh.dataDescription = spec.description;
  dh.subSpecification = spec.subSpec;
  dh.payloadSize = 0;

  DataProcessingHeader dph{mContext->timeslice(), 1};
  //we have to move the incoming data
  o2::Header::Stack headerStack{dh, dph};
  FairMQMessagePtr headerMessage = mDevice->NewMessageFor(channel, 0,
                                                          headerStack.buffer.get(),
                                                          headerStack.bufferSize,
                                                          &o2::Header::Stack::freefn,
                                                          headerStack.buffer.get());
  headerStack.buffer.release();

  auto payload = std::make_unique<TClonesArray>(className, nElements);
  payload->SetOwner(kTRUE);
  auto &result = *payload.get();
  mRootContext->addObject(std::move(headerMessage), std::move(payload), channel);
  assert(payload.get() == nullptr);
  return result;
}

}
}
