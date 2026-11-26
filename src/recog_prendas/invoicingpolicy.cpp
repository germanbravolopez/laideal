#include "recog_prendas.h"

#include "verifactuintegration.h"

AddGarment::InvoicingPolicy invoicingPolicyOf(VerifactuIntegration *integration)
{
    AddGarment::InvoicingPolicy policy;
    policy.invoicingBlocked = [integration] {
        return integration ? integration->invoicingBlockedReason() : QString();
    };
    policy.invoiceDateRefused = [integration](const QDate &date) {
        return integration ? integration->dateBeforeProductionReason(date) : QString();
    };
    policy.productionStartDate = [integration] {
        return integration ? integration->productionStartDate() : QDate();
    };
    policy.aeatEnvironment = [integration] {
        return integration ? integration->environmentName() : QString();
    };
    return policy;
}
