Manea Andrei Iulian 331CB

Pentru implementarea temei, am țiunt cont de modularitatea codului și am creat următoarele fișiere:
- main.c -> conține funcția main a programului
- functions.c -> conține funcțiile threadurilor și funcțiile auxiliare folosite pentru implementare
- header.h -> conține librăriile și structurile utilizate și antetele funcțiilor auxiliare

Voi detalia implementarea din cele două fișiere .c:

## main.c
Am inițializat datele de intrare în variabile și am citit datele din fișierul de input.
Am folosit structura FileInfo pentru a reține numele și ID-ul fișierelor la citire. Am parsat numele fișierelor cu prefixul ../checker/ pentru a pune în evidență calea absolută și a fi recunoscute la rulare.
Am folosit structurile:
    - MapperArgs pentru a avea la îndemână variabilele necesare thread-urilor Mapper
    - ReducerArgs pentru a avea la îndemână variabilele necesare thread-urilor Reducer
    - ThreadArgs pentru a putea transmite în thread_function un singur argument
Am creat thread-urile, acestea își îndeplinesc task-urile, apoi dau joi. La final, am luat în considerare memory management-ul și am eliberat memoria folosită.

## functions.c
1. thread_function
Am verificat dacă thread-ul este Mapper sau Reducer în funcție de ID-ul său. În funcție de asta, el execută funția aferentă.
Am implementat o barieră care se asigură că toate thread-urile Mapper își termină execuția înainte ca cele Reducer să o înceapă pe a lor.

2. mapper_function
Am utilizat o listă locală pentru fiecare thread, unde sunt stocate intrările de tip {cuvânt, id_fișier}. La final, vor fi merge-uite într-o listă globală.
Am folosit o variabilă atomică pentru a identifica ID-ul fișierului curent de procesat, astfel încât să nu fie accesat același fișier de două thread-uri.
Fiecare fișier procesat este citit cuvânt cu cuvânt, cuvintele sunt parsate corespunzător și adăugate în lista locală.
Operațiunile sunt făcute într-un loop while, care merge atât timp cât încă sunt fișiere de procesat.
La final, listele locale sunt merge-uite într-o listă globală, unde voi avea intrări de tipul { cuvânt1, [ id_fișier1, id_fișier2, ... ] }.
Ultima operație trebuie sincronizată folosind un mutex, astfel încât lista globală să fie modificată de un singur thread at a time.

3. parse_word
Aduce următoarele modificări unui cuvânt:
    - transformă literele mari în litere mici
    - elimină din cuvânt cifrele, semnele de punctuație și alte caractere non-alfabetice

4. add_word_to_list
Funția adaugă un cuvânt într-o listă locală.
Se verifică mai întâi dacă acel cuvânt există deja în listă. Dacă da, se verifică dacă are assignat ID-ul fișierului curent, dacă da, se trece mai departe.
Dacă nu are ID-ul assignat, se adaugă. Dacă cuvântul nu există, se creează o nouă intrare în listă cu el.

5. merge_local_list_into_global
Funcția merge-uiește o listă locală în lista globală cu toate cuvintele.
Se verifică mai întâi dacă acel cuvânt există deja în listă. Dacă da, se verifică dacă are assignat ID-ul fișierului curent, dacă da, se trece mai departe.
Dacă nu are ID-ul assignat, se adaugă. Dacă cuvântul nu există, se creează o nouă intrare în listă cu el.
La final, se eliberază memoria folosită de lista locală.

6. reducer_function
Am împărțit workload-ul fiecărui thread, astfel încât munca să fie distribuită egal.
Intrările din lista globală se grupează în bucket-uri în funcție de inițiala fiecărui cuvânt. Se utilizează un mutex pentru ca lista să fie accesată de un singur thread at a time.
Pentru literele assignate fiecărui thread, acesta creează fișierele de output, sortează cuvintele de la fiecare după numărul de fișiere în care apar și apoi alfabetic.
La final, cuvintele sunt scrise în fișiere și este eliberată memoria folosită.

7. compare_words
Sortează cuvintele după numărul de apariții și alfabetic.

8. compare_file_ids
Sortează ID-urile fișierelor în care un cuvânt apare în ordine crescătoare.