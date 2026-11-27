#include <app/ShellVerbs.h>

#include <app/Engine.h>
#include <app/PauseMenu.h>

VerbAdmission RuntimeResumeOperation::Resume()
{
    // Nothing open is nothing to resume. Refused rather than accepted silently:
    // a producer that fired a resume at a running game should be able to see
    // that it did.
    if (!Menu->IsOpen())
        return VerbAdmission::Refused;

    Menu->RequestResume();
    return VerbAdmission::Accepted;
}

VerbAdmission ApplicationQuitOperation::Quit()
{
    Host->RequestExit(Engine::ExitSource::Menu);
    return VerbAdmission::Accepted;
}
