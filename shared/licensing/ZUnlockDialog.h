#pragma once

#include <JuceHeader.h>
#include "shared/ui/theme/ZarlinoLookAndFeel.h"
#include "ZLicenseManager.h"

namespace Zarlino::Licensing {

class ZUnlockDialog : public juce::Component
{
public:
    ZUnlockDialog()
    {
        setSize(500, 380);

        keyEditor_.setMultiLine(true, false);
        keyEditor_.setReturnKeyStartsNewLine(false);
        keyEditor_.setTextToShowWhenEmpty("Paste your license key here...", juce::Colour (Zarlino::Palette::steel_400));
        keyEditor_.setColour(juce::TextEditor::textColourId, juce::Colour (Zarlino::Palette::mist_100));
        keyEditor_.setColour(juce::TextEditor::outlineColourId, juce::Colour (Zarlino::Palette::graphite_700));
        keyEditor_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour (Zarlino::Palette::electric_blue));
        addAndMakeVisible(keyEditor_);

        unlockButton_.setButtonText("Unlock");
        unlockButton_.setColour(juce::TextButton::buttonColourId, juce::Colour (Zarlino::Palette::electric_blue));
        unlockButton_.setColour(juce::TextButton::textColourOnId, juce::Colour (Zarlino::Palette::mist_100));
        unlockButton_.onClick = [this] { applyKeyFromEditor(); };
        addAndMakeVisible(unlockButton_);

        loadFileButton_.setButtonText("Load License File...");
        loadFileButton_.setColour(juce::TextButton::buttonColourId, juce::Colour (Zarlino::Palette::graphite_800));
        loadFileButton_.setColour(juce::TextButton::textColourOffId, juce::Colour (Zarlino::Palette::mist_100));
        loadFileButton_.onClick = [this] { loadLicenseFromFile(); };
        addAndMakeVisible(loadFileButton_);

        statusLabel_.setJustificationType(juce::Justification::centredLeft);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour (Zarlino::Palette::steel_300));
        statusLabel_.setFont(juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible(statusLabel_);

        purchaseLink_.setColour(juce::HyperlinkButton::textColourId, juce::Colour (Zarlino::Palette::electric_blue));
        addAndMakeVisible(purchaseLink_);

        updateStatusText();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour (Zarlino::Palette::obsidian_950));

        auto r = getLocalBounds();
        auto logoArea = r.removeFromTop(80);
        g.setColour(juce::Colour (Zarlino::Palette::electric_blue));
        g.fillRoundedRectangle(logoArea.reduced (12, 12).toFloat(), 6.0f);

        g.setColour(juce::Colour (Zarlino::Palette::mist_100));
        g.setFont(juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText("Zarlino Audio", logoArea.reduced (24, 0), juce::Justification::centredLeft);

        g.setColour(juce::Colour (Zarlino::Palette::steel_300));
        g.setFont(juce::Font (juce::FontOptions (12.0f)));
        g.drawText("Enter your license key to unlock", getLocalBounds().removeFromTop(80).reduced(24, 0),
                   juce::Justification::bottomLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(20);
        r.removeFromTop(80);
        r.removeFromTop(8);
        keyEditor_.setBounds(r.removeFromTop(100));
        r.removeFromTop(12);
        auto btnRow = r.removeFromTop(36);
        unlockButton_.setBounds(btnRow.removeFromRight(120));
        loadFileButton_.setBounds(btnRow.removeFromRight(180).reduced(0, 0));
        r.removeFromTop(12);
        statusLabel_.setBounds(r.removeFromTop(24));
        r.removeFromTop(8);
        purchaseLink_.setBounds(r.removeFromTop(20).removeFromLeft(160));
    }

private:
    void applyKeyFromEditor()
    {
        juce::String key = keyEditor_.getText().trim();
        if (key.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "License Key", "Please paste a license key.");
            return;
        }

        bool ok = ZLicenseManager::getInstance().applyLicenseKey(key);
        if (ok)
        {
            updateStatusText();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "License Applied", "License applied successfully.");
            if (auto* w = juce::Component::getTopLevelComponent())
                if (auto* dw = dynamic_cast<juce::DialogWindow*>(w))
                    dw->exitModalState(0);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "License Key", "Invalid or mismatched license key.");
            updateStatusText();
        }
    }

    void loadLicenseFromFile()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select license file...",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result == juce::File())
                    return; // cancelled

                bool ok = ZLicenseManager::getInstance().applyLicenseFile(result);
                if (ok)
                {
                    updateStatusText();
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                        "License Applied", "License file applied successfully.");
                    if (auto* w = juce::Component::getTopLevelComponent())
                        if (auto* dw = dynamic_cast<juce::DialogWindow*>(w))
                            dw->exitModalState(0);
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "License File", "Failed to apply license file.");
                    updateStatusText();
                }
            });
    }

    void updateStatusText()
    {
        auto info = ZLicenseManager::getInstance().getStatusInfo();
        switch (info.state)
        {
            case LicenseState::Licensed:
            {
                juce::String email = info.info.licenseeEmail;
                statusLabel_.setText("Licensed to: " + (email.isNotEmpty() ? email : juce::String("(unknown)")),
                    juce::dontSendNotification);
                break;
            }
            case LicenseState::Trial:
            {
                statusLabel_.setText("Trial \xe2\x80\x94 " + juce::String(info.daysRemaining) + " days remaining",
                    juce::dontSendNotification);
                break;
            }
            case LicenseState::TrialExpired:
                statusLabel_.setText("Trial Expired \xe2\x80\x94 purchase to unlock full version",
                    juce::dontSendNotification);
                break;
            case LicenseState::Invalid:
            default:
                statusLabel_.setText("Not licensed", juce::dontSendNotification);
                break;
        }
    }

    juce::Image logo_;
    juce::TextEditor keyEditor_;
    juce::TextButton unlockButton_;
    juce::TextButton loadFileButton_;
    juce::Label statusLabel_;
    juce::HyperlinkButton purchaseLink_{"Purchase", juce::URL("https://zarlino.example/buy")};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZUnlockDialog)
};

} // namespace Zarlino::Licensing
