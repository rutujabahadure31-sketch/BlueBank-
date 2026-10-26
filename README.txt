BLUEBANK DEMO DEPLOYMENT FILES

Files:
- server.c: Linux-compatible C backend; reads PORT environment variable and binds to 0.0.0.0.
- Dockerfile: builds and runs the backend in a container.

Important:
This is an educational demo, not a real banking system. The API has no authentication and allows public account reads and changes. Use fictional names and demo balances only. Anyone with the deployed API URL may create accounts or change demo balances. Do not use real customer or financial data.

The backend writes accounts.txt and transactions.txt in its current working directory. On hosting services, local container files may be temporary and may be lost when the service restarts or is redeployed unless persistent storage is configured.

Deploy with a host that supports Docker. Set the web service's port using the PORT environment variable if required. After deployment, test the backend URL and then change the CodePen API_BASE constant to the deployed backend URL (without a trailing slash).

Endpoints:
GET /
GET /api/accounts
GET /api/create?name=TestCustomer&deposit=500
GET /api/deposit?account=100001&amount=200
GET /api/withdraw?account=100001&amount=100
GET /api/balance?account=100001
GET /api/transactions?account=100001
