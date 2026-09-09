# Data sent to the Inlay backend

This inventory covers application-defined request data in the current JUCE
module. HTTP transport-generated details such as `Content-Length` are not
listed. The module cannot observe how the backend consumes a field; where the
client protocol does not show a functional dependency, the purpose is listed as
**backend troubleshooting**.

## Request headers

Every POST request sends these headers:

| Header | Purpose |
| --- | --- |
| `Content-Type: application/json` | Identifies the request body format. |
| `X-Inlay-Product-Id` | Product identity; **backend troubleshooting**. The product ID is also included in relevant request bodies. |
| `X-Inlay-Module-Version` | Module-version context; **backend troubleshooting**. |
| `X-Inlay-Instance-Id` | Per-unlocker-instance correlation; **backend troubleshooting**. |

## Authentication and access requests

`POST app/auth/start` sends:

| JSON field | Purpose |
| --- | --- |
| `productId` | Identifies the product for which activation is being started. |
| `deviceId` | Identifies the device that will be bound to the product access token. |

`POST app/auth/complete` sends `activationToken` plus the metadata fields below.
`POST app/auth/access` sends `idToken` plus the same metadata fields.

| JSON field | Purpose |
| --- | --- |
| `activationToken` | Continues the activation session created by `app/auth/start`. |
| `idToken` | Proves the existing signed-in/activated identity when requesting or refreshing access. |
| `moduleVersion` | Module-version context; **backend troubleshooting**. |
| `deviceId` | Device identity used for access-token device binding. |
| `os` | Operating-system context; **backend troubleshooting**. |
| `productId` | Identifies the product whose access is requested. |
| `productVersion` | Product-version context; **backend troubleshooting**. |
| `productName` | Human-readable product context; **backend troubleshooting**. |
| `isPlugin` | Indicates whether the requesting product is a plugin; **backend troubleshooting**. |
| `sdkVersion` | JUCE SDK version; **backend troubleshooting**. |
| `instanceID` | Per-unlocker-instance correlation; **backend troubleshooting**. |

## Browser authentication URL

The module opens `GET appweb/auth/continue` in the browser with:

| Query parameter | Purpose |
| --- | --- |
| `activationToken` | Identifies the activation session to continue in the browser. |
| `redirectURL` | Local callback URL to which the browser flow redirects after completion. |
