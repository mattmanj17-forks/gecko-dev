/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <UIKit/UIColor.h>
#import <UIKit/UIInterface.h>

#include "nsLookAndFeel.h"

#include "mozilla/FontPropertyTypes.h"
#include "nsStyleConsts.h"
#include "gfxFont.h"
#include "gfxFontConstants.h"

using namespace mozilla;

nsLookAndFeel::nsLookAndFeel() : mInitialized(false) {}

nsLookAndFeel::~nsLookAndFeel() {}

static nscolor GetColorFromUIColor(UIColor* aColor) {
  CGColorRef cgColor = [aColor CGColor];
  CGColorSpaceModel model = CGColorSpaceGetModel(CGColorGetColorSpace(cgColor));
  const CGFloat* components = CGColorGetComponents(cgColor);
  if (model == kCGColorSpaceModelRGB) {
    return NS_RGB((unsigned int)(components[0] * 255.0),
                  (unsigned int)(components[1] * 255.0),
                  (unsigned int)(components[2] * 255.0));
  } else if (model == kCGColorSpaceModelMonochrome) {
    unsigned int val = (unsigned int)(components[0] * 255.0);
    return NS_RGBA(val, val, val, (unsigned int)(components[1] * 255.0));
  }
  MOZ_ASSERT_UNREACHABLE("Unhandled color space!");
  return 0;
}

void nsLookAndFeel::NativeInit() { EnsureInit(); }

void nsLookAndFeel::RefreshImpl() {
  nsXPLookAndFeel::RefreshImpl();

  mInitialized = false;
}

nsresult nsLookAndFeel::NativeGetColor(ColorID aID, ColorScheme aColorScheme,
                                       nscolor& aResult) {
  EnsureInit();

  nsresult res = NS_OK;

  switch (aID) {
    case ColorID::Highlight:
      aResult = NS_RGB(0xaa, 0xaa, 0xaa);
      break;
    case ColorID::MozMenuhover:
      aResult = NS_RGB(0xee, 0xee, 0xee);
      break;
    case ColorID::Highlighttext:
    case ColorID::MozMenuhovertext:
      aResult = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case ColorID::IMESelectedRawTextBackground:
    case ColorID::IMESelectedConvertedTextBackground:
    case ColorID::IMERawInputBackground:
    case ColorID::IMEConvertedTextBackground:
      aResult = NS_TRANSPARENT;
      break;
    case ColorID::IMESelectedRawTextForeground:
    case ColorID::IMESelectedConvertedTextForeground:
    case ColorID::IMERawInputForeground:
    case ColorID::IMEConvertedTextForeground:
      aResult = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case ColorID::IMERawInputUnderline:
    case ColorID::IMEConvertedTextUnderline:
      aResult = NS_40PERCENT_FOREGROUND_COLOR;
      break;
    case ColorID::IMESelectedRawTextUnderline:
    case ColorID::IMESelectedConvertedTextUnderline:
      aResult = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case ColorID::SpellCheckerUnderline:
      aResult = NS_RGB(0xff, 0, 0);
      break;

    //
    // css2 system colors http://www.w3.org/TR/REC-CSS2/ui.html#system-colors
    //
    case ColorID::Buttontext:
    case ColorID::MozButtonhovertext:
    case ColorID::Captiontext:
    case ColorID::Menutext:
    case ColorID::Infotext:
    case ColorID::Windowtext:
      aResult = mColorDarkText;
      break;
    case ColorID::Activecaption:
      aResult = NS_RGB(0xff, 0xff, 0xff);
      break;
    case ColorID::Activeborder:
      aResult = NS_RGB(0x00, 0x00, 0x00);
      break;
    case ColorID::Appworkspace:
      aResult = NS_RGB(0xFF, 0xFF, 0xFF);
      break;
    case ColorID::Background:
      aResult = NS_RGB(0x63, 0x63, 0xCE);
      break;
    case ColorID::Buttonface:
    case ColorID::MozButtonhoverface:
      aResult = NS_RGB(0xF0, 0xF0, 0xF0);
      break;
    case ColorID::Buttonhighlight:
      aResult = NS_RGB(0xFF, 0xFF, 0xFF);
      break;
    case ColorID::Buttonshadow:
      aResult = NS_RGB(0xDC, 0xDC, 0xDC);
      break;
    case ColorID::Graytext:
      aResult = NS_RGB(0x44, 0x44, 0x44);
      break;
    case ColorID::Inactiveborder:
      aResult = NS_RGB(0xff, 0xff, 0xff);
      break;
    case ColorID::Inactivecaption:
      aResult = NS_RGB(0xaa, 0xaa, 0xaa);
      break;
    case ColorID::Inactivecaptiontext:
      aResult = NS_RGB(0x45, 0x45, 0x45);
      break;
    case ColorID::Scrollbar:
      aResult = NS_RGB(0, 0, 0);  // XXX
      break;
    case ColorID::Threeddarkshadow:
      aResult = NS_RGB(0xDC, 0xDC, 0xDC);
      break;
    case ColorID::Threedshadow:
      aResult = NS_RGB(0xE0, 0xE0, 0xE0);
      break;
    case ColorID::Threedface:
      aResult = NS_RGB(0xF0, 0xF0, 0xF0);
      break;
    case ColorID::Threedhighlight:
      aResult = NS_RGB(0xff, 0xff, 0xff);
      break;
    case ColorID::Threedlightshadow:
      aResult = NS_RGB(0xDA, 0xDA, 0xDA);
      break;
    case ColorID::Menu:
      aResult = NS_RGB(0xff, 0xff, 0xff);
      break;
    case ColorID::Infobackground:
      aResult = NS_RGB(0xFF, 0xFF, 0xC7);
      break;
    case ColorID::Windowframe:
      aResult = NS_RGB(0xaa, 0xaa, 0xaa);
      break;
    case ColorID::Window:
    case ColorID::Field:
    case ColorID::MozCombobox:
      aResult = NS_RGB(0xff, 0xff, 0xff);
      break;
    case ColorID::Fieldtext:
    case ColorID::MozComboboxtext:
      aResult = mColorDarkText;
      break;
    case ColorID::MozDialog:
      aResult = NS_RGB(0xaa, 0xaa, 0xaa);
      break;
    case ColorID::MozDialogtext:
    case ColorID::MozCellhighlighttext:
    case ColorID::Selecteditemtext:
    case ColorID::MozColheadertext:
    case ColorID::MozColheaderhovertext:
      aResult = mColorDarkText;
      break;
    case ColorID::MozMacFocusring:
      aResult = NS_RGB(0x3F, 0x98, 0xDD);
      break;
    case ColorID::MozMacDisabledtoolbartext:
      aResult = NS_RGB(0x3F, 0x3F, 0x3F);
      break;
    case ColorID::MozCellhighlight:
    case ColorID::Selecteditem:
      // For inactive list selection
      aResult = NS_RGB(0xaa, 0xaa, 0xaa);
      break;
    case ColorID::MozOddtreerow:
      // Background color of odd list rows.
      aResult = NS_TRANSPARENT;
      break;
    case ColorID::Visitedtext:
      // Safari defaults to the MacOS color implementation for visited links,
      // which in turn uses systemPurpleColor, so we do the same here.
      aResult = GetColorFromUIColor([UIColor systemPurpleColor]);
      break;
    case ColorID::Linktext:
    case ColorID::Activetext:
    case ColorID::TargetTextBackground:
    case ColorID::TargetTextForeground:
      aResult = GetStandinForNativeColor(aID, aColorScheme);
      break;
    default:
      NS_WARNING("Someone asked nsILookAndFeel for a color I don't know about");
      aResult = NS_RGB(0xff, 0xff, 0xff);
      res = NS_ERROR_FAILURE;
      break;
  }

  return res;
}

NS_IMETHODIMP
nsLookAndFeel::NativeGetInt(IntID aID, int32_t& aResult) {
  nsresult res = NS_OK;

  switch (aID) {
    case IntID::ScrollButtonLeftMouseButtonAction:
      aResult = 0;
      break;
    case IntID::ScrollButtonMiddleMouseButtonAction:
    case IntID::ScrollButtonRightMouseButtonAction:
      aResult = 3;
      break;
    case IntID::CaretBlinkTime:
      aResult = 567;
      break;
    case IntID::CaretWidth:
      aResult = 1;
      break;
    case IntID::SelectTextfieldsOnKeyFocus:
      // Select textfield content when focused by kbd
      // used by nsEventStateManager::sTextfieldSelectModel
      aResult = 1;
      break;
    case IntID::SubmenuDelay:
      aResult = 200;
      break;
    case IntID::MenusCanOverlapOSBar:
      // xul popups are not allowed to overlap the menubar.
      aResult = 0;
      break;
    case IntID::SkipNavigatingDisabledMenuItem:
      aResult = 1;
      break;
    case IntID::DragThresholdX:
    case IntID::DragThresholdY:
      aResult = 4;
      break;
    case IntID::ScrollArrowStyle:
      aResult = eScrollArrow_None;
      break;
    case IntID::UseOverlayScrollbars:
    case IntID::AllowOverlayScrollbarsOverlap:
      aResult = 1;
      break;
    case IntID::ScrollbarDisplayOnMouseMove:
      aResult = 0;
      break;
    case IntID::ScrollbarFadeBeginDelay:
      aResult = 450;
      break;
    case IntID::ScrollbarFadeDuration:
      aResult = 200;
      break;
    case IntID::TreeOpenDelay:
      aResult = 1000;
      break;
    case IntID::TreeCloseDelay:
      aResult = 1000;
      break;
    case IntID::TreeLazyScrollDelay:
      aResult = 150;
      break;
    case IntID::TreeScrollDelay:
      aResult = 100;
      break;
    case IntID::TreeScrollLinesMax:
      aResult = 3;
      break;
    case IntID::ScrollToClick:
      aResult = 0;
      break;
    case IntID::ChosenMenuItemsShouldBlink:
      aResult = 1;
      break;
    case IntID::IMERawInputUnderlineStyle:
    case IntID::IMEConvertedTextUnderlineStyle:
    case IntID::IMESelectedRawTextUnderlineStyle:
    case IntID::IMESelectedConvertedTextUnderline:
      aResult = static_cast<int32_t>(StyleTextDecorationStyle::Solid);
      break;
    case IntID::SpellCheckerUnderlineStyle:
      aResult = static_cast<int32_t>(StyleTextDecorationStyle::Dotted);
      break;
    case IntID::ContextMenuOffsetVertical:
    case IntID::ContextMenuOffsetHorizontal:
      aResult = 2;
      break;
    default:
      aResult = 0;
      res = NS_ERROR_FAILURE;
  }
  return res;
}

NS_IMETHODIMP
nsLookAndFeel::NativeGetFloat(FloatID aID, float& aResult) {
  nsresult res = NS_OK;

  switch (aID) {
    case FloatID::IMEUnderlineRelativeSize:
      aResult = 2.0f;
      break;
    case FloatID::SpellCheckerUnderlineRelativeSize:
      aResult = 2.0f;
      break;
    default:
      aResult = -1.0;
      res = NS_ERROR_FAILURE;
  }

  return res;
}

bool nsLookAndFeel::NativeGetFont(FontID aID, nsString& aFontName,
                                  gfxFontStyle& aFontStyle) {
  // hack for now
  if (aID == FontID::Caption || aID == FontID::Menu) {
    aFontStyle.style = FontSlantStyle::NORMAL;
    aFontStyle.weight = FontWeight::NORMAL;
    aFontStyle.stretch = FontStretch::NORMAL;
    aFontStyle.size = 14;
    aFontStyle.systemFont = true;

    aFontName.AssignLiteral("sans-serif");
    return true;
  }

  // TODO: implement more here?
  return false;
}

void nsLookAndFeel::EnsureInit() {
  if (mInitialized) {
    return;
  }
  mInitialized = true;

  mColorDarkText = GetColorFromUIColor([UIColor darkTextColor]);

  RecordTelemetry();
}
