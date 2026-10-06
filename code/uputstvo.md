# Eksperiment 2: epoll i io_uring

Tri C programa, svaki sa kodom u main(). Rezultati se dodaju u tekstualne fajlove pomoću fprintf().

## Prevođenje

```bash
sudo apt install gcc make liburing-dev
make
```

## Pokretanje

Prvi terminal:

```bash
./server_epoll 8 0 64 10000
```

Drugi terminal, iz istog foldera:

```bash
./client 8 0 64 10000 client_epoll.txt
```

Kada se oba završe, prvi terminal:

```bash
./server_uring 8 0 64 10000
```

Drugi terminal:

```bash
./client 8 0 64 10000 client_uring.txt
```

Argumenti su: aktivne konekcije, neaktivne konekcije, bajtovi po poruci i poruke po aktivnoj konekciji. Kod servera i klijenta moraju biti isti. Poslednji argument klijenta je samo ime izlaznog fajla. Serveri koriste isti port, pa ih pokrećeš odvojeno.

## Kako radi

Oba servera prvo prihvate sve konekcije običnim accept(). Zatim pošalju znak S klijentu i počinje merena razmena. Otvaranje konekcija nije deo merenja. Po aktivnoj konekciji klijent čeka celu vraćenu poruku pre slanja sledeće; neaktivne veze ostaju otvorene.

Server vraća pristigle bajtove. Ako prenese samo deo, pamti koliko je već poslato i nastavlja kasnije. epoll prijavi spremnost, a program poziva recv/send sa MSG_DONTWAIT. io_uring zada operacije i preuzima CQE-ove; dostupne naredne operacije šalje zajedno. Klijent koristi poll() i proverava svaki vraćeni bajt.

## Rezultati

Serveri pišu u epoll.txt i io_uring.txt.
- `messages`: broj poruka po aktivnoj konekciji.
- `messages_s`: poruke u sekundi; za poređenje koristi vrednost klijenta.
- `average_response_us`: prosečno vreme do prijema celog odgovora, iz klijentskog fajla.
- `MiB_s`: vraćeni sadržaj u jednom smeru, iz servera.
- `cpu_percent`: CPU vreme procesa u odnosu na trajanje testa.
- `cpu_us_message`: CPU vreme servera po poruci.