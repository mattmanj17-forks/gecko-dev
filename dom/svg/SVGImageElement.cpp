/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGImageElement.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/gfx/2D.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "imgINotificationObserver.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/SVGImageElementBinding.h"
#include "mozilla/dom/SVGLengthBinding.h"
#include "mozilla/dom/UserActivation.h"
#include "nsContentUtils.h"
#include "SVGGeometryProperty.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(Image)

using namespace mozilla::gfx;

namespace mozilla::dom {

JSObject* SVGImageElement::WrapNode(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return SVGImageElement_Binding::Wrap(aCx, this, aGivenProto);
}

SVGElement::LengthInfo SVGImageElement::sLengthInfo[4] = {
    {nsGkAtoms::x, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGContentUtils::X},
    {nsGkAtoms::y, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGContentUtils::Y},
    {nsGkAtoms::width, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGContentUtils::X},
    {nsGkAtoms::height, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGContentUtils::Y},
};

SVGElement::StringInfo SVGImageElement::sStringInfo[2] = {
    {nsGkAtoms::href, kNameSpaceID_None, true},
    {nsGkAtoms::href, kNameSpaceID_XLink, true}};

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ISUPPORTS_INHERITED(SVGImageElement, SVGImageElementBase,
                            imgINotificationObserver, nsIImageLoadingContent)

//----------------------------------------------------------------------
// Implementation

namespace SVGT = SVGGeometryProperty::Tags;

SVGImageElement::SVGImageElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : SVGImageElementBase(std::move(aNodeInfo)) {
  // We start out broken
  AddStatesSilently(ElementState::BROKEN);
}

SVGImageElement::~SVGImageElement() { nsImageLoadingContent::Destroy(); }

nsCSSPropertyID SVGImageElement::GetCSSPropertyIdForAttrEnum(
    uint8_t aAttrEnum) {
  switch (aAttrEnum) {
    case ATTR_X:
      return eCSSProperty_x;
    case ATTR_Y:
      return eCSSProperty_y;
    case ATTR_WIDTH:
      return eCSSProperty_width;
    case ATTR_HEIGHT:
      return eCSSProperty_height;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown attr enum");
      return eCSSProperty_UNKNOWN;
  }
}
//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGImageElement)

void SVGImageElement::NodeInfoChanged(Document* aOldDoc) {
  SVGImageElementBase::NodeInfoChanged(aOldDoc);

  // Reparse the URI if needed. Note that we can't check whether we already have
  // a parsed URI, because it might be null even if we have a valid href
  // attribute, if we tried to parse with a different base.
  UpdateSrcURI();

  QueueImageTask(mSrcURI, /* aAlwaysLoad = */ true, /* aNotify = */ false);
}

//----------------------------------------------------------------------

already_AddRefed<DOMSVGAnimatedLength> SVGImageElement::X() {
  return mLengthAttributes[ATTR_X].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGImageElement::Y() {
  return mLengthAttributes[ATTR_Y].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGImageElement::Width() {
  return mLengthAttributes[ATTR_WIDTH].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGImageElement::Height() {
  return mLengthAttributes[ATTR_HEIGHT].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedPreserveAspectRatio>
SVGImageElement::PreserveAspectRatio() {
  return mPreserveAspectRatio.ToDOMAnimatedPreserveAspectRatio(this);
}

already_AddRefed<DOMSVGAnimatedString> SVGImageElement::Href() {
  return mStringAttributes[HREF].IsExplicitlySet()
             ? mStringAttributes[HREF].ToDOMAnimatedString(this)
             : mStringAttributes[XLINK_HREF].ToDOMAnimatedString(this);
}

void SVGImageElement::GetDecoding(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::decoding, kDecodingTableDefault->tag, aValue);
}

already_AddRefed<Promise> SVGImageElement::Decode(ErrorResult& aRv) {
  return nsImageLoadingContent::QueueDecodeAsync(aRv);
}

//----------------------------------------------------------------------

void SVGImageElement::UpdateSrcURI() {
  nsAutoString href;
  if (mStringAttributes[HREF].IsExplicitlySet()) {
    mStringAttributes[HREF].GetAnimValue(href, this);
  } else {
    mStringAttributes[XLINK_HREF].GetAnimValue(href, this);
  }

  mSrcURI = nullptr;
  if (!href.IsEmpty()) {
    StringToURI(href, OwnerDoc(), getter_AddRefs(mSrcURI));
  }
}

void SVGImageElement::LoadSelectedImage(bool aAlwaysLoad,
                                        bool aStopLazyLoading) {
  nsresult rv = NS_ERROR_FAILURE;

  const bool kNotify = true;
  if (mSrcURI || (mStringAttributes[HREF].IsExplicitlySet() ||
                  mStringAttributes[XLINK_HREF].IsExplicitlySet())) {
    rv = LoadImage(mSrcURI, /* aForce = */ true, kNotify, eImageLoadType_Normal,
                   LoadFlags(), OwnerDoc());
  }

  if (NS_FAILED(rv)) {
    CancelImageRequests(kNotify);
  }
}

Rect SVGImageElement::GeometryBounds(const Matrix& aToBoundsSpace) {
  Rect rect;

  DebugOnly<bool> ok =
      SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width,
                                      SVGT::Height>(this, &rect.x, &rect.y,
                                                    &rect.width, &rect.height);
  MOZ_ASSERT(ok, "SVGGeometryProperty::ResolveAll failed");

  if (rect.IsEmpty()) {
    // Rendering of the element disabled
    rect.SetEmpty();  // Make sure width/height are zero and not negative
  }

  return aToBoundsSpace.TransformBounds(rect);
}

//----------------------------------------------------------------------
// EventTarget methods:

void SVGImageElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  nsImageLoadingContent::AsyncEventRunning(aEvent);
}

//----------------------------------------------------------------------
//  nsImageLoadingContent methods:

CORSMode SVGImageElement::GetCORSMode() {
  return AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));
}

void SVGImageElement::GetFetchPriority(nsAString& aFetchPriority) const {
  GetEnumAttr(nsGkAtoms::fetchpriority, kFetchPriorityAttributeValueAuto,
              aFetchPriority);
}

//----------------------------------------------------------------------
// nsIContent methods:

bool SVGImageElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                     const nsAString& aValue,
                                     nsIPrincipal* aMaybeScriptedPrincipal,
                                     nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::crossorigin) {
      ParseCORSValue(aValue, aResult);
      return true;
    }
    if (aAttribute == nsGkAtoms::decoding) {
      return aResult.ParseEnumValue(aValue, kDecodingTable, false,
                                    kDecodingTableDefault);
    }
    if (aAttribute == nsGkAtoms::fetchpriority) {
      ParseFetchPriority(aValue, aResult);
      return true;
    }
  }

  return SVGImageElementBase::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                             aMaybeScriptedPrincipal, aResult);
}

void SVGImageElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                   const nsAttrValue* aValue,
                                   const nsAttrValue* aOldValue,
                                   nsIPrincipal* aSubjectPrincipal,
                                   bool aNotify) {
  bool forceReload = false;
  if (aName == nsGkAtoms::href && (aNamespaceID == kNameSpaceID_None ||
                                   aNamespaceID == kNameSpaceID_XLink)) {
    if (aNamespaceID == kNameSpaceID_XLink &&
        mStringAttributes[HREF].IsExplicitlySet()) {
      // href overrides xlink:href
      return;
    }
    UpdateSrcURI();
    forceReload = true;
  } else if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::decoding) {
      // Request sync or async image decoding.
      SetSyncDecodingHint(
          aValue && static_cast<ImageDecodingType>(aValue->GetEnumValue()) ==
                        ImageDecodingType::Sync);
    } else if (aName == nsGkAtoms::crossorigin) {
      forceReload = GetCORSMode() != AttrValueToCORSMode(aOldValue);
    }
  }

  if (forceReload) {
    mUseUrgentStartForChannel = UserActivation::IsHandlingUserInput();
    QueueImageTask(mSrcURI, /* aAlwaysLoad = */ true, aNotify);
  }

  return SVGImageElementBase::AfterSetAttr(
      aNamespaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

nsresult SVGImageElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = SVGImageElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  nsImageLoadingContent::BindToTree(aContext, aParent);

  return rv;
}

void SVGImageElement::UnbindFromTree(UnbindContext& aContext) {
  nsImageLoadingContent::UnbindFromTree();
  SVGImageElementBase::UnbindFromTree(aContext);
}

void SVGImageElement::DestroyContent() {
  ClearImageLoadTask();

  nsImageLoadingContent::Destroy();
  SVGImageElementBase::DestroyContent();
}

NS_IMETHODIMP_(bool)
SVGImageElement::IsAttributeMapped(const nsAtom* name) const {
  return IsInLengthInfo(name, sLengthInfo) ||
         SVGImageElementBase::IsAttributeMapped(name);
}

//----------------------------------------------------------------------
// SVGElement methods

/* virtual */
bool SVGImageElement::HasValidDimensions() const {
  float width, height;

  if (SVGGeometryProperty::ResolveAll<SVGT::Width, SVGT::Height>(this, &width,
                                                                 &height)) {
    return width > 0 && height > 0;
  }
  // This function might be called for an element in display:none subtree
  // (e.g. SMIL animateMotion), we fall back to use SVG attributes.
  return (!mLengthAttributes[ATTR_WIDTH].IsExplicitlySet() ||
          mLengthAttributes[ATTR_WIDTH].GetAnimValInSpecifiedUnits() > 0) &&
         (!mLengthAttributes[ATTR_HEIGHT].IsExplicitlySet() ||
          mLengthAttributes[ATTR_HEIGHT].GetAnimValInSpecifiedUnits() > 0);
}

SVGElement::LengthAttributesInfo SVGImageElement::GetLengthInfo() {
  return LengthAttributesInfo(mLengthAttributes, sLengthInfo,
                              std::size(sLengthInfo));
}

SVGAnimatedPreserveAspectRatio*
SVGImageElement::GetAnimatedPreserveAspectRatio() {
  return &mPreserveAspectRatio;
}

SVGElement::StringAttributesInfo SVGImageElement::GetStringInfo() {
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              std::size(sStringInfo));
}

void SVGImageElement::DidAnimateAttribute(int32_t aNameSpaceID,
                                          nsAtom* aAttribute) {
  if ((aNameSpaceID == kNameSpaceID_None ||
       aNameSpaceID == kNameSpaceID_XLink) &&
      aAttribute == nsGkAtoms::href) {
    UpdateSrcURI();
    mUseUrgentStartForChannel = false;
    QueueImageTask(mSrcURI, /* aAlwaysLoad = */ true, /* aNotify = */ true);
  }
  SVGImageElementBase::DidAnimateAttribute(aNameSpaceID, aAttribute);
}

}  // namespace mozilla::dom
