#include "UI/Sections/ModalSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>
#include <string>

namespace Demo {

using namespace PandaUI;

ModalSection::ModalSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ModalSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Modal", "Blocking overlays: Context::modals().present(...). The UI under a modal receives no input; "
                 "Escape closes it, Enter triggers the primary action of the dialog."
    ));

    m_resultLabel = std::make_shared<Label>("Result: —");
    m_resultLabel->setFont(Font(Style::BodyFont));
    m_resultLabel->setTextRole(TextRole::Secondary);
    Label *result = m_resultLabel.get();

    {
        auto example = std::make_shared<DemoExample>("Dialog: confirmation", "Title + message + buttons by role.");
        auto open = std::make_shared<Button>("Delete…");
        open->setOnClick([result](Button &button) {
            Context *context = button.getContext();
            if (!context) { return; }
            auto dialog = std::make_shared<Dialog>("Delete 3 assets?");
            dialog->setMessage("The files will be deleted from disk. This action cannot be undone.");
            dialog->addAction("Cancel", DialogActionRole::Cancel, [result] { result->setText("Result: Cancel"); });
            dialog->addAction("Delete", DialogActionRole::Destructive, [result] { result->setText("Result: Delete"); });
            context->modals().present(dialog);
        });
        example->addContent(open);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Dialog: form", "The body is any view. The primary button is disabled until the form is valid."
        );
        auto open = std::make_shared<Button>("New Layer…");
        open->setOnClick([result](Button &button) {
            Context *context = button.getContext();
            if (!context) { return; }
            auto dialog = std::make_shared<Dialog>("New Layer");

            auto form = std::make_shared<Panel>();
            form->setBackgroundColor(Color(0x00000000));
            form->layout().setFlexDirection(FlexDirection::Column);
            form->layout().setGap(8.f);
            auto nameField = std::make_shared<TextField>();
            nameField->setPlaceholder("Layer name");
            form->addSubview(std::make_shared<PropertyRow>("Name", nameField));
            auto kind = std::make_shared<Combobox>();
            kind->setItems({"Grass", "Bush", "Tree"});
            kind->setSelectedIndex(0);
            form->addSubview(std::make_shared<PropertyRow>("Kind", kind));
            dialog->setBody(form);

            dialog->addAction("Cancel", DialogActionRole::Cancel);
            TextField *name = nameField.get();
            Combobox *kindPtr = kind.get();
            auto create = dialog->addAction("Create", DialogActionRole::Primary, [result, name, kindPtr] {
                result->setText("Result: " + name->getText() + " / #" + std::to_string(kindPtr->selectedIndex()));
            });
            create->setEnabled(false);
            Button *createPtr = create.get();
            nameField->setOnTextChanged([createPtr](TextField &field) { createPtr->setEnabled(!field.getText().empty()); });
            dialog->setInitialFocus(nameField.get());
            context->modals().present(dialog);
        });
        example->addContent(open);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Modal: arbitrary content", "setDismissesOnOutsideClick(true) — pickers close on a click on the backdrop."
        );
        auto open = std::make_shared<Button>("Pick…");
        open->setOnClick([result](Button &button) {
            Context *context = button.getContext();
            if (!context) { return; }
            auto card = std::make_shared<Panel>();
            card->setBackgroundColor(ThemeColor::surface);
            card->surface().setCornerRadius(10.f);
            card->layout().setFlexDirection(FlexDirection::Row);
            card->layout().setPadding(16.f);
            card->layout().setGap(8.f);
            auto modal = std::make_shared<Modal>(card);
            modal->setDismissesOnOutsideClick(true);
            Modal *modalPtr = modal.get();
            for (const char *title : {"Alpha", "Beta", "Gamma"}) {
                auto option = std::make_shared<Button>(title);
                option->setOnClick([result, modalPtr](Button &picked) {
                    result->setText("Result: " + picked.getText());
                    modalPtr->dismiss();
                });
                card->addSubview(option);
            }
            context->modals().present(modal);
        });
        example->addContent(open);
        addSubview(example);
    }
    addSubview(m_resultLabel);
}

void ModalSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ModalSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
